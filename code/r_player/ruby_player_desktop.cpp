/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of/or use in source or binary forms (partially or complete) must retain
          the above copyright notice, this list of conditions and the following disclaimer
          in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form must reproduce the above copyright notice, this
          list of conditions and the following disclaimer in the documentation and/or other
          materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the
          user interface, additions could be made to that info.
        * Neither the name of the organization nor the
          names of its contributors may be used to endorse or promote products
          derived from this software without specific prior written permission.
        * Military use is not permitted.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE AUTHOR (PETRU SOROAGA) BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Desktop video player.
//
// The Raspberry Pi and the Radxa have a hardware video decoder, so their players
// feed the video stream to the hardware decoder and draw the result directly.
// The desktop has no hardware decoder, so this player uses GStreamer to decode
// the stream and to show it in a real window.
//
// The video stream that reaches the player is a raw video elementary stream
// (H.264 or H.265) in the Annex-B format: no container, no timestamps, the
// frames are delimited only by the NAL start codes. The player does not change
// the stream and does not add anything to it, it only pushes the bytes it reads
// to a GStreamer pipeline. The pipeline detects the codec from the stream
// itself, so the same player works for H.264 and for H.265.
//
// The command line is the same as the command line of the other players of the
// project, so the station and the central application start and stop it the
// same way they do it on the other platforms:
//    -p       read the live video stream from the named pipe
//    -sm      read the live video stream from the shared memory
//    -file    play a file instead of the live stream
//    -fps     the frame rate of the file
//    -h265    the stream is H.265 (only a hint, the codec is auto detected)
//    -endexit signal the playback semaphores when the file playback ends
//    -af      cpu affinity mask
//    -rawp    raw priority
//    -d       write the log to the standard output
//    -ver     print the version and exit
//
// The stream is read from the same objects the other players use:
//    - the named pipe FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY
//    - the shared memory SM_STREAMER_NAME, with the semaphore
//      SEMAPHORE_SM_VIDEO_DATA_AVAILABLE
//
// The video sink is a real window: waylandsink on Wayland, ximagesink on X11,
// and autovideosink as the fallback.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_obj_names.h"
#include "../base/hardware.h"
#include "../base/hardware_procs.h"

static volatile sig_atomic_t s_bQuit = false;
static bool s_bDebug = false;
static bool s_bPlayStreamPipe = false;
static bool s_bPlayStreamSM = false;
static bool s_bPlayFile = false;
static bool s_bUseH265Decoder = false;
static bool s_bExitOnEnd = false;
static char s_szPlayFileName[MAX_FILE_PATH_SIZE] = {0};
static int s_iFileFPS = 30;
static u32 s_uCPUAffinityMask = 0;
static int s_iRawPriority = -1;

#define PLAYER_READ_BUFFER_SIZE 65536
#define PLAYER_APPSRC_MAX_BYTES (8*1024*1024)

static u8 s_uReadBuffer[PLAYER_READ_BUFFER_SIZE];

static GstElement* s_pPipeline = NULL;
static GstElement* s_pAppSrc = NULL;
static GstElement* s_pDecoder = NULL;
static GstElement* s_pConvert = NULL;
static GstElement* s_pSink = NULL;
static GstBus* s_pBus = NULL;
static bool s_bVideoPadLinked = false;
static bool s_bCodecDetected = false;

static u32 s_uBytesRead = 0;
static u32 s_uBytesPushed = 0;
static u32 s_uDecodedFrames = 0;
static u32 s_uTimeLastLog = 0;


static void _player_handle_signal(int iSignal)
{
   (void)iSignal;
   s_bQuit = true;
}


// Counts the frames that reach the video converter, it is used only for the
// log, to prove that the stream is really decoded and not only forwarded.
static GstPadProbeReturn _player_frame_probe(GstPad* pPad, GstPadProbeInfo* pInfo, gpointer pData)
{
   (void)pPad;
   (void)pInfo;
   (void)pData;
   s_uDecodedFrames++;
   return GST_PAD_PROBE_OK;
}


// The decoder is a decodebin, so the codec is detected from the stream. When
// the decoder knows the codec it creates a pad with the decoded video, that pad
// has to be linked to the converter.
static void _player_on_decoder_pad_added(GstElement* pDecoder, GstPad* pPad, gpointer pData)
{
   (void)pData;

   if ( s_bVideoPadLinked )
      return;

   GstCaps* pCaps = gst_pad_get_current_caps(pPad);
   if ( NULL == pCaps )
      pCaps = gst_pad_query_caps(pPad, NULL);
   if ( NULL == pCaps )
      return;

   char* szCaps = gst_caps_to_string(pCaps);
   if ( (NULL != szCaps) && (NULL != strstr(szCaps, "video")) )
   {
      log_line("[PlayerDesktop] Detected the video stream format: %s", szCaps);
      s_bVideoPadLinked = true;
      s_bCodecDetected = true;

      GstPad* pSinkPad = gst_element_get_static_pad(s_pConvert, "sink");
      if ( NULL != pSinkPad )
      {
         if ( 0 != gst_pad_link(pPad, pSinkPad) )
            log_softerror_and_alarm("[PlayerDesktop] Failed to link the decoder to the video converter.");
         gst_object_unref(pSinkPad);
      }
   }

   if ( NULL != szCaps )
      g_free(szCaps);
   gst_caps_unref(pCaps);
}


// The callback of the GStreamer bus. It returns FALSE for the messages that
// have to stop the pipeline.
static gboolean _player_bus_callback(GstBus* pBus, GstMessage* pMessage, gpointer pData)
{
   (void)pBus;
   (void)pData;

   if ( GST_MESSAGE_TYPE(pMessage) == GST_MESSAGE_ERROR )
   {
      GError* pError = NULL;
      gchar* pDebug = NULL;
      gst_message_parse_error(pMessage, &pError, &pDebug);
      log_error_and_alarm("[PlayerDesktop] The video pipeline failed: %s (%s)",
         (NULL != pError) ? pError->message : "unknown error",
         (NULL != pDebug) ? pDebug : "");
      if ( NULL != pError )
         g_error_free(pError);
      if ( NULL != pDebug )
         g_free(pDebug);
      s_bQuit = true;
      return FALSE;
   }

   if ( GST_MESSAGE_TYPE(pMessage) == GST_MESSAGE_EOS )
   {
      log_line("[PlayerDesktop] The video pipeline reached the end of the stream.");
      s_bQuit = true;
      return FALSE;
   }

   return TRUE;
}


// Pushes the bytes that were read from the source to the pipeline.
// Returns 0 on success and -1 if the pipeline can not take the data any more.
static int _player_push_buffer(const u8* pData, int iLength)
{
   if ( (NULL == pData) || (iLength <= 0) )
      return 0;

   GstBuffer* pBuffer = gst_buffer_new_allocate(NULL, iLength, NULL);
   if ( NULL == pBuffer )
   {
      log_softerror_and_alarm("[PlayerDesktop] Failed to allocate the GStreamer buffer.");
      return -1;
   }
   gst_buffer_fill(pBuffer, 0, pData, iLength);

   GstFlowReturn iFlow = gst_app_src_push_buffer(GST_APP_SRC(s_pAppSrc), pBuffer);
   s_uBytesPushed += iLength;
   if ( GST_FLOW_OK != iFlow )
   {
      // The pipeline can not take the data any more. This happens when the
      // window was closed or when the pipeline was stopped.
      log_line("[PlayerDesktop] The pipeline did not accept the data (flow: %d). Closing the player.", iFlow);
      return -1;
   }
   return 0;
}


// Creates the GStreamer pipeline that decodes the stream and shows it in a
// window. The decoder is a decodebin, so the codec is detected from the
// stream, the same player works for H.264 and for H.265.
static int _player_create_pipeline(bool bLiveStream)
{
   s_pPipeline = gst_pipeline_new("ruby-player-desktop");
   if ( NULL == s_pPipeline )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to create the GStreamer pipeline.");
      return -1;
   }

   s_pAppSrc = gst_element_factory_make("appsrc", "ruby-video-source");
   s_pDecoder = gst_element_factory_make("decodebin", "ruby-video-decoder");
   s_pConvert = gst_element_factory_make("videoconvert", "ruby-video-convert");

   // The video sink is a real window. On Wayland it is the waylandsink, on X11
   // it is the ximagesink, and the autovideosink is the fallback.
   s_pSink = gst_element_factory_make("waylandsink", "ruby-video-sink");
   if ( NULL == s_pSink )
      s_pSink = gst_element_factory_make("ximagesink", "ruby-video-sink");
   if ( NULL == s_pSink )
      s_pSink = gst_element_factory_make("autovideosink", "ruby-video-sink");

   if ( (NULL == s_pAppSrc) || (NULL == s_pDecoder) || (NULL == s_pConvert) || (NULL == s_pSink) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to create the elements of the GStreamer pipeline. Is GStreamer installed?");
      return -1;
   }

   gst_bin_add_many(GST_BIN(s_pPipeline), s_pAppSrc, s_pDecoder, s_pConvert, s_pSink, NULL);
   if ( ! gst_element_link(s_pAppSrc, s_pDecoder) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to link the source to the decoder.");
      return -1;
   }
   if ( ! gst_element_link(s_pConvert, s_pSink) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to link the video converter to the video sink.");
      return -1;
   }
   g_signal_connect(s_pDecoder, "pad-added", G_CALLBACK(_player_on_decoder_pad_added), NULL);

   // The stream is a raw elementary stream, without a container and without
   // timestamps, so the source is a byte stream.
   g_object_set(s_pAppSrc, "format", GST_FORMAT_BYTES, "block", FALSE, NULL);
   g_object_set(s_pAppSrc, "max-bytes", (guint64)PLAYER_APPSRC_MAX_BYTES, NULL);
   g_object_set(s_pAppSrc, "leaky-type", GST_APP_LEAKY_TYPE_UPSTREAM, NULL);

   if ( bLiveStream )
   {
      // A live stream has to be displayed as it arrives, and if the decoder can
      // not keep up, the old frames have to be dropped, not the new ones.
      g_object_set(s_pAppSrc, "is-live", TRUE, "do-timestamp", TRUE, NULL);
      g_object_set(s_pSink, "sync", FALSE, NULL);
   }
   else
   {
      // A file is played at the rate it was recorded at.
      g_object_set(s_pAppSrc, "is-live", FALSE, NULL);
      g_object_set(s_pSink, "sync", TRUE, NULL);
   }

   GstPad* pSinkPad = gst_element_get_static_pad(s_pConvert, "sink");
   if ( NULL != pSinkPad )
   {
      gst_pad_add_probe(pSinkPad, GST_PAD_PROBE_TYPE_BUFFER, _player_frame_probe, NULL, NULL);
      gst_object_unref(pSinkPad);
   }

   s_pBus = gst_pipeline_get_bus(GST_PIPELINE(s_pPipeline));
   gst_bus_add_watch(s_pBus, _player_bus_callback, NULL);

   log_line("[PlayerDesktop] GStreamer pipeline created: appsrc ! decodebin ! videoconvert ! video sink");
   return 0;
}


// Signals the semaphores that the central application uses to know that the
// playback of a file is about to end and that it ended.
static void _player_signal_play_file_will_finish()
{
   if ( ! s_bExitOnEnd )
      return;

   sem_t* pSemaphore = sem_open(SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( (NULL != pSemaphore) && (SEM_FAILED != pSemaphore) )
   {
      log_line("[PlayerDesktop] Signaling the semaphore that the playback will finish (%s)", SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH);
      sem_post(pSemaphore);
      sem_close(pSemaphore);
   }
   else
      log_softerror_and_alarm("[PlayerDesktop] Failed to open and signal the semaphore %s", SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH);
   hardware_sleep_ms(100);
}


static void _player_signal_play_file_finished()
{
   if ( ! s_bExitOnEnd )
      return;

   sem_t* pSemaphore = sem_open(SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( (NULL != pSemaphore) && (SEM_FAILED != pSemaphore) )
   {
      log_line("[PlayerDesktop] Signaling the semaphore that the playback finished (%s)", SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED);
      sem_post(pSemaphore);
      sem_close(pSemaphore);
   }
   else
      log_softerror_and_alarm("[PlayerDesktop] Failed to open and signal the semaphore %s", SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED);
}


// Reads the live video stream from the named pipe and pushes it to the
// pipeline. The pipe is opened for reading, the same pipe the station writes
// the video stream to. The player stays alive when the writer closes the pipe,
// because the station closes and reopens it when the video parameters change.
static int _player_run_pipe()
{
   log_line("[PlayerDesktop] Opening the video stream pipe: %s", FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY);

   // The pipe is created by the station when it opens the write end, so it
   // might not exist yet when the player starts. The player waits for it, the
   // stream can start at any time.
   int fd = -1;
   int iRetries = 0;
   while ( (fd < 0) && (! s_bQuit) )
   {
      iRetries++;
      fd = open(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, O_RDONLY | O_NONBLOCK);
      if ( fd < 0 )
      {
         if ( 1 == (iRetries % 500) )
            log_line("[PlayerDesktop] Waiting for the video stream pipe to appear...");
         hardware_sleep_ms(10);
      }
   }

   if ( fd < 0 )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to open the video stream pipe: %s, error: %d (%s)",
         FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, errno, strerror(errno));
      return -1;
   }
   log_line("[PlayerDesktop] Opened the video stream pipe, fd: %d", fd);

   bool bAnyDataReceived = false;

   while ( ! s_bQuit )
   {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      int iPoll = poll(&pfd, 1, 100);
      if ( iPoll < 0 )
      {
         if ( EINTR == errno )
            continue;
         log_softerror_and_alarm("[PlayerDesktop] Failed to wait for data on the video stream pipe, error: %d (%s)", errno, strerror(errno));
         break;
      }

      if ( 0 == iPoll )
      {
         // No data yet, but the player has to stay alive, the stream can start
         // at any time.
         if ( s_bDebug && (g_TimeNow > s_uTimeLastLog + 1000) )
         {
            s_uTimeLastLog = g_TimeNow;
            log_line("[PlayerDesktop] Waiting for video stream data. Bytes read: %u, bytes pushed: %u", s_uBytesRead, s_uBytesPushed);
         }
         continue;
      }

      if ( 0 != (pfd.revents & POLLERR) )
      {
         log_softerror_and_alarm("[PlayerDesktop] Error on the video stream pipe. Waiting for it to be reopened.");
         close(fd);
         fd = -1;
         while ( (fd < 0) && (! s_bQuit) )
         {
            fd = open(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, O_RDONLY | O_NONBLOCK);
            if ( fd < 0 )
               hardware_sleep_ms(10);
         }
         if ( fd < 0 )
            break;
         continue;
      }

      int iRead = read(fd, s_uReadBuffer, PLAYER_READ_BUFFER_SIZE);
      if ( iRead > 0 )
      {
         s_uBytesRead += iRead;
         if ( ! bAnyDataReceived )
         {
            bAnyDataReceived = true;
            log_line("[PlayerDesktop] Received the first video stream data (%u bytes so far).", s_uBytesRead);
         }
         if ( 0 != _player_push_buffer(s_uReadBuffer, iRead) )
            break;
         continue;
      }

      if ( 0 == iRead )
      {
         // There is no writer on the pipe at this moment. The station can open
         // the write end again at any time (for example when the video
         // resolution changes), so the player waits instead of exiting.
         if ( s_bDebug && (g_TimeNow > s_uTimeLastLog + 1000) )
         {
            s_uTimeLastLog = g_TimeNow;
            log_line("[PlayerDesktop] The video stream pipe has no writer at this moment. Waiting for it. Bytes read: %u.", s_uBytesRead);
         }
         hardware_sleep_ms(20);
         continue;
      }

      if ( EINTR == errno )
         continue;

      if ( EAGAIN == errno )
         continue;

      log_softerror_and_alarm("[PlayerDesktop] Failed to read from the video stream pipe, error: %d (%s)", errno, strerror(errno));
      break;
   }

   if ( fd >= 0 )
      close(fd);
   return 0;
}


// Reads the live video stream from the shared memory and pushes it to the
// pipeline. The shared memory is a ring buffer, the first two u32 are the
// write position (written twice, so a torn write can be detected) and the data
// starts after them.
static int _player_run_sm()
{
   log_line("[PlayerDesktop] Opening the video stream shared memory: %s", SM_STREAMER_NAME);

   sem_t* pSemaphore = sem_open(SEMAPHORE_SM_VIDEO_DATA_AVAILABLE, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( (NULL == pSemaphore) || (SEM_FAILED == pSemaphore) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to open the semaphore for the video stream shared memory, error: %d (%s)", errno, strerror(errno));
      return -1;
   }

   int fdSM = shm_open(SM_STREAMER_NAME, O_RDONLY, S_IRUSR | S_IWUSR);
   if ( fdSM < 0 )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to open the video stream shared memory for reading: %s, error: %d (%s)", SM_STREAMER_NAME, errno, strerror(errno));
      sem_close(pSemaphore);
      return -1;
   }

   u8* pSMem = (u8*) mmap(NULL, SM_STREAMER_SIZE, PROT_READ, MAP_SHARED, fdSM, 0);
   close(fdSM);
   if ( (NULL == pSMem) || (MAP_FAILED == pSMem) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to map the video stream shared memory, error: %d (%s)", errno, strerror(errno));
      sem_close(pSemaphore);
      return -1;
   }
   log_line("[PlayerDesktop] Mapped the video stream shared memory.");

   u32 uReadPos = 2*sizeof(u32);
   bool bAnyDataReceived = false;

   while ( ! s_bQuit )
   {
      u32* pWritePos1 = (u32*)(&(pSMem[0]));
      u32* pWritePos2 = (u32*)(&(pSMem[sizeof(u32)]));
      u32 uWritePos1 = *pWritePos1;
      u32 uWritePos2 = *pWritePos2;

      // The two write positions have to be the same, otherwise the writer is
      // in the middle of a write and the data is not complete yet.
      if ( (uReadPos == uWritePos1) || (uWritePos1 != uWritePos2) )
      {
         struct timespec ts;
         clock_gettime(CLOCK_REALTIME, &ts);
         ts.tv_nsec += 1000LL*(long long)10000;
         if ( ts.tv_nsec >= 1000000000L )
         {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
         }
         if ( 0 != sem_timedwait(pSemaphore, &ts) )
         {
            if ( (ETIMEDOUT != errno) && (! s_bQuit) )
               log_softerror_and_alarm("[PlayerDesktop] Failed to wait on the video stream semaphore, error: %d (%s)", errno, strerror(errno));
            continue;
         }
         is_semaphore_signaled_clear_logok(pSemaphore, SEMAPHORE_SM_VIDEO_DATA_AVAILABLE, 0);

         uWritePos1 = *pWritePos1;
         uWritePos2 = *pWritePos2;
         if ( (uReadPos == uWritePos1) || (uWritePos1 != uWritePos2) )
            continue;
      }

      int iBytesToRead = 0;
      // The data available is the distance from the read position to the
      // published write position, going forward in the ring. The ring is
      // read in one continuous run, wrapping at the end of the segment: the
      // bytes are never skipped and never read twice.
      int iBytesAvailable = (int)(uWritePos1 - uReadPos);
      if ( iBytesAvailable < 0 )
         iBytesAvailable += SM_STREAMER_SIZE;
      if ( iBytesAvailable > PLAYER_READ_BUFFER_SIZE )
         iBytesAvailable = PLAYER_READ_BUFFER_SIZE;
      if ( iBytesAvailable <= 0 )
      {
         uReadPos = 2*sizeof(u32);
         continue;
      }

      // The first part: from the read position to the end of the ring.
      int iBytesToReadTail = (int)(SM_STREAMER_SIZE - uReadPos);
      if ( iBytesToReadTail > iBytesAvailable )
         iBytesToReadTail = iBytesAvailable;
      if ( iBytesToReadTail > 0 )
         memcpy(s_uReadBuffer, &(pSMem[uReadPos]), iBytesToReadTail);

      // The second part: from the start of the ring, if the writer wrapped
      // around the end of the segment.
      int iBytesToReadHead = iBytesAvailable - iBytesToReadTail;
      if ( iBytesToReadHead > 0 )
         memcpy(&(s_uReadBuffer[iBytesToReadTail]), &(pSMem[2*sizeof(u32)]), iBytesToReadHead);

      uReadPos += iBytesAvailable;
      if ( uReadPos >= SM_STREAMER_SIZE )
         uReadPos = 2*sizeof(u32) + (uReadPos - SM_STREAMER_SIZE);
      iBytesToRead = iBytesAvailable;

      s_uBytesRead += iBytesToRead;
      if ( ! bAnyDataReceived )
      {
         bAnyDataReceived = true;
         log_line("[PlayerDesktop] Received the first video stream data (%u bytes so far).", s_uBytesRead);
      }

      if ( 0 != _player_push_buffer(s_uReadBuffer, iBytesToRead) )
         break;

      if ( s_bDebug && (g_TimeNow > s_uTimeLastLog + 1000) )
      {
         s_uTimeLastLog = g_TimeNow;
         log_line("[PlayerDesktop] Video stream: %u bytes read, %u bytes pushed, %u frames decoded.", s_uBytesRead, s_uBytesPushed, s_uDecodedFrames);
      }
   }

   munmap(pSMem, SM_STREAMER_SIZE);
   sem_close(pSemaphore);
   return 0;
}


// Plays a file that has a raw video stream in it.
static int _player_run_file()
{
   log_line("[PlayerDesktop] Opening the video file: %s", s_szPlayFileName);

   int fd = open(s_szPlayFileName, O_RDONLY);
   if ( fd < 0 )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to open the video file: %s, error: %d (%s)", s_szPlayFileName, errno, strerror(errno));
      return -1;
   }

   while ( ! s_bQuit )
   {
      int iRead = read(fd, s_uReadBuffer, PLAYER_READ_BUFFER_SIZE);
      if ( iRead > 0 )
      {
         s_uBytesRead += iRead;
         if ( 0 != _player_push_buffer(s_uReadBuffer, iRead) )
            break;
         continue;
      }
      if ( 0 == iRead )
      {
         log_line("[PlayerDesktop] The video file was read completely. Bytes read: %u", s_uBytesRead);
         break;
      }
      if ( EINTR == errno )
         continue;
      log_softerror_and_alarm("[PlayerDesktop] Failed to read the video file, error: %d (%s)", errno, strerror(errno));
      break;
   }

   close(fd);
   return 0;
}


int main(int argc, char* argv[])
{
   // The signal handler is installed with sigaction and without SA_RESTART, so
   // that a blocking read is interrupted when the player is asked to stop.
   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = _player_handle_signal;
   sigemptyset(&(sa.sa_mask));
   sa.sa_flags = 0;
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGQUIT, &sa, NULL);
   signal(SIGPIPE, SIG_IGN);

   if ( (argc > 1) && (0 == strcmp(argv[argc-1], "-ver")) )
   {
      printf("%d.%d (b-%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER);
      return 0;
   }

   if ( argc < 2 )
   {
      printf("\nUsage: ruby_player_desktop [params]\nParams:\n\n");
      printf("-p Play the live video stream from the pipe\n");
      printf("-sm Play the live video stream from the sharedmem\n");
      printf("-h265 use the H265 decoder\n");
      printf("-file [filename] play a file\n");
      printf("-fps [fps]\n");
      printf("-endexit exit on end\n");
      printf("-af [aff] cpu affinity\n");
      printf("-rawp [prio] raw priority\n");
      printf("-d debug output to stdout\n");
      printf("-ver print the version\n\n");
      return 0;
   }

   for( int i=1; i<argc; i++ )
   {
      if ( 0 == strcmp(argv[i], "-p") )
         s_bPlayStreamPipe = true;
      else if ( 0 == strcmp(argv[i], "-sm") )
         s_bPlayStreamSM = true;
      else if ( 0 == strcmp(argv[i], "-h265") )
         s_bUseH265Decoder = true;
      else if ( 0 == strcmp(argv[i], "-endexit") )
         s_bExitOnEnd = true;
      else if ( 0 == strcmp(argv[i], "-d") )
         s_bDebug = true;
      else if ( (0 == strcmp(argv[i], "-fps")) && (i+1 < argc) )
      {
         s_iFileFPS = atoi(argv[++i]);
         if ( (s_iFileFPS < 10) || (s_iFileFPS > 240) )
            s_iFileFPS = 30;
      }
      else if ( (0 == strcmp(argv[i], "-af")) && (i+1 < argc) )
         s_uCPUAffinityMask = (u32)atoi(argv[++i]);
      else if ( (0 == strcmp(argv[i], "-rawp")) && (i+1 < argc) )
         s_iRawPriority = atoi(argv[++i]);
      else if ( (0 == strcmp(argv[i], "-file")) && (i+1 < argc) )
      {
         s_bPlayFile = true;
         strncpy(s_szPlayFileName, argv[++i], MAX_FILE_PATH_SIZE-1);
         s_szPlayFileName[MAX_FILE_PATH_SIZE-1] = 0;
         if ( NULL != strstr(s_szPlayFileName, ".h265") )
            s_bUseH265Decoder = true;
      }
   }

   if ( s_bPlayFile )
      log_init("RubyPlayerF");
   else if ( s_bPlayStreamPipe )
      log_init("RubyPlayerP");
   else if ( s_bPlayStreamSM )
      log_init("RubyPlayerS");
   else
      log_init("RubyPlayer");

   if ( s_bDebug )
      log_enable_stdout();

   log_line("[PlayerDesktop] Started, version: %d.%d (b-%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER);
   log_line("[PlayerDesktop] Raw priority param: %d, cpu affinity mask param: %d", s_iRawPriority, s_uCPUAffinityMask);
   if ( s_bUseH265Decoder )
      log_line("[PlayerDesktop] The video stream is expected to be H.265. The codec is detected from the stream anyway.");

   if ( (s_iRawPriority > 1) && (s_iRawPriority < 100) )
   {
      struct sched_param params;
      params.sched_priority = 100 - s_iRawPriority;
      if ( 0 != pthread_setschedparam(pthread_self(), SCHED_FIFO, &params) )
         log_softerror_and_alarm("[PlayerDesktop] Failed to set the real time priority of the player.");
      else
         log_line("[PlayerDesktop] Set the real time priority of the player to: %d", params.sched_priority);
   }

   if ( s_uCPUAffinityMask > 0 )
   {
      cpu_set_t cpuSet;
      CPU_ZERO(&cpuSet);
      for( int i=0; i<(int)hw_procs_get_cpu_count(); i++ )
      {
         if ( s_uCPUAffinityMask & (0x01<<i) )
            CPU_SET(i, &cpuSet);
      }
      if ( 0 != sched_setaffinity(0, sizeof(cpuSet), &cpuSet) )
         log_softerror_and_alarm("[PlayerDesktop] Failed to set the cpu affinity of the player.");
      else
         log_line("[PlayerDesktop] Set the cpu affinity of the player to the mask: %u", s_uCPUAffinityMask);
   }

   gst_init(&argc, &argv);

   bool bLiveStream = (! s_bPlayFile);
   if ( 0 != _player_create_pipeline(bLiveStream) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to create the video pipeline. Exit.");
      return 1;
   }

   if ( GST_STATE_CHANGE_FAILURE == gst_element_set_state(s_pPipeline, GST_STATE_PLAYING) )
   {
      log_error_and_alarm("[PlayerDesktop] Failed to start the video pipeline. Exit.");
      return 1;
   }
   log_line("[PlayerDesktop] The video pipeline is playing.");

   int iRes = 0;
   if ( s_bPlayFile )
   {
      iRes = _player_run_file();
      // The file was read completely, tell the central application that the
      // playback is about to end, then let the pipeline show the last frames.
      _player_signal_play_file_will_finish();
      gst_app_src_end_of_stream(GST_APP_SRC(s_pAppSrc));
      u32 uTimeStart = get_current_timestamp_ms();
      while ( (! s_bQuit) && (get_current_timestamp_ms() < uTimeStart + 5000) )
         hardware_sleep_ms(20);
      _player_signal_play_file_finished();
   }
   else if ( s_bPlayStreamSM )
   {
      iRes = _player_run_sm();
      gst_app_src_end_of_stream(GST_APP_SRC(s_pAppSrc));
   }
   else
   {
      iRes = _player_run_pipe();
      gst_app_src_end_of_stream(GST_APP_SRC(s_pAppSrc));
   }

   // Give the pipeline a short time to show the data that is left.
   u32 uTimeStart = get_current_timestamp_ms();
   while ( get_current_timestamp_ms() < uTimeStart + 500 )
      hardware_sleep_ms(20);

   gst_element_set_state(s_pPipeline, GST_STATE_NULL);
   if ( NULL != s_pBus )
   {
      gst_bus_remove_watch(s_pBus);
      gst_object_unref(s_pBus);
      s_pBus = NULL;
   }
   if ( NULL != s_pPipeline )
   {
      gst_object_unref(s_pPipeline);
      s_pPipeline = NULL;
   }

   log_line("[PlayerDesktop] Exit. Video stream: %u bytes read, %u bytes pushed, %u frames decoded, codec detected: %s.",
      s_uBytesRead, s_uBytesPushed, s_uDecodedFrames, s_bCodecDetected?"yes":"no");
   return (0 == iRes) ? 0 : 1;
}
