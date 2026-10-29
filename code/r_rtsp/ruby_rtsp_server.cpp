/*
    Ruby Licence
    Copyright (c) 2020-2026 Petru Soroaga
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
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

/*
    RubyRTSPServer

    Serves the live video stream received by the ground station over RTSP, so
    any GCS software (KiteGCS, QGroundControl, Mission Planner, VLC, ...) can
    connect to it and display the video.

    URL: rtsp://<ground station ip>:<port>/video   (default port: 8554)

    The video is not decoded or re-encoded: the raw elementary stream (H.264
    or H.265) received from the vehicle is read from the named pipe
    FIFO_RUBY_STATION_VIDEO_STREAM_RTSP (the station feeds it in parallel with
    the local video player) and is packetized to RTP by the RTSP server.

    Command line arguments:
       -port N   the RTSP TCP listening port (default 8554)
       -h265     the stream is H.265 (default is H.264)
       -d        write the log to the standard output
       -ver      print the version and exit
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <glib-unix.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_obj_names.h"
#include "../base/hardware.h"
#include "../base/ruby_ipc.h"

static volatile sig_atomic_t s_bQuit = false;
static bool s_bDebug = false;
static bool s_bUseH265 = false;
static int s_iPort = 8554;

static GMainLoop* s_pMainLoop = NULL;

static void _handle_signal(int sig)
{
   (void)sig;
   s_bQuit = true;
   if ( NULL != s_pMainLoop )
      g_main_loop_quit(s_pMainLoop);
}

// Messages from the media pipeline. Logs everything and exits the server on
// pipeline errors or end-of-stream: a shared pipeline that has failed can
// never recover by itself (it would serve nothing to any connecting client).
// The station watchdog relaunches a fresh server process a few seconds later.
static void _on_media_element_message(GstRTSPMedia* pMedia, GstMessage* pMsg, gpointer user_data)
{
   (void)pMedia;
   (void)user_data;

   if ( GST_MESSAGE_ERROR == pMsg->type )
   {
      GError* pErr = NULL;
      gchar* szDebug = NULL;
      gst_message_parse_error(pMsg, &pErr, &szDebug);
      log_softerror_and_alarm("[RTSPServer] Pipeline error from element [%s]: %s%s%s",
         (NULL != pMsg->src) ? GST_OBJECT_NAME(pMsg->src) : "NULL", (NULL != pErr) ? pErr->message : "",
         (NULL != szDebug) ? " | " : "", (NULL != szDebug) ? szDebug : "");
      if ( NULL != pErr )
         g_error_free(pErr);
      if ( NULL != szDebug )
         g_free(szDebug);
      log_line("[RTSPServer] Exiting so the station can relaunch a fresh server.");
      s_bQuit = true;
      if ( NULL != s_pMainLoop )
         g_main_loop_quit(s_pMainLoop);
   }
   else if ( GST_MESSAGE_EOS == pMsg->type )
      log_softerror_and_alarm("[RTSPServer] The media pipeline got end of stream (EOS).");
   else if ( GST_MESSAGE_WARNING == pMsg->type )
   {
      GError* pErr = NULL;
      gchar* szDebug = NULL;
      gst_message_parse_warning(pMsg, &pErr, &szDebug);
      log_line("[RTSPServer] Pipeline warning from element [%s]: %s",
         (NULL != pMsg->src) ? GST_OBJECT_NAME(pMsg->src) : "NULL",
         (NULL != pErr) ? pErr->message : "");
      if ( NULL != pErr )
         g_error_free(pErr);
      if ( NULL != szDebug )
         g_free(szDebug);
   }
}

static void _on_media_configure(GstRTSPMediaFactory* pFactory, GstRTSPMedia* pMedia, gpointer user_data)
{
   (void)pFactory;
   (void)user_data;
   g_signal_connect(pMedia, "element-message", G_CALLBACK(_on_media_element_message), NULL);
   log_line("[RTSPServer] Media pipeline configured.");
}

static void _on_client_connect(GstRTSPMediaFactory* pFactory, GstRTSPMedia* pMedia, gpointer user_data)
{
   (void)pFactory;
   (void)pMedia;
   (void)user_data;
   log_line("[RTSPServer] A client connected to the stream.");
}

static void _parse_arguments(int argc, char* argv[])
{
   for( int i=1; i<argc; i++ )
   {
      if ( 0 == strcmp(argv[i], "-h265") )
         s_bUseH265 = true;
      else if ( 0 == strcmp(argv[i], "-d") )
         s_bDebug = true;
      else if ( 0 == strcmp(argv[i], "-ver") )
      {
         printf("RubyRTSPServer version %d.%d (b-%d)\n", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER);
         exit(0);
      }
      else if ( 0 == strcmp(argv[i], "-port") )
      {
         i++;
         if ( i < argc )
            s_iPort = atoi(argv[i]);
         if ( s_iPort < 1 || s_iPort > 65535 )
            s_iPort = 8554;
      }
      else
         log_line("Unknown argument: [%s]", argv[i]);
   }
}

int main(int argc, char* argv[])
{
   signal(SIGINT, _handle_signal);
   signal(SIGTERM, _handle_signal);
   signal(SIGPIPE, SIG_IGN);

   _parse_arguments(argc, argv);

   log_init("RubyRTSPServer");
   if ( s_bDebug )
      log_enable_stdout();
   log_line("[RTSPServer] Started, version: %d.%d (b-%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER);
   log_line("[RTSPServer] Starting (PID: %d, parent PID: %d)...", getpid(), getppid());
   if ( s_bUseH265 )
      log_line("[RTSPServer] Video stream type: H.265");
   else
      log_line("[RTSPServer] Video stream type: H.264");

   // Make sure the input pipe is a real FIFO pipe. The station opens its
   // write endpoint as soon as this FIFO exists (and retries until this
   // server has the read endpoint opened), so this must be done before
   // building the pipeline.
   if ( ! ruby_ensure_fifo_pipe(FIFO_RUBY_STATION_VIDEO_STREAM_RTSP) )
      log_error_and_alarm("[RTSPServer] Failed to create the video stream pipe %s", FIFO_RUBY_STATION_VIDEO_STREAM_RTSP);

   gst_init(&argc, &argv);

   GstRTSPServer* pServer = gst_rtsp_server_new();
   if ( NULL == pServer )
   {
      log_error_and_alarm("Failed to create the RTSP server.");
      return -1;
   }

   char szPort[32];
   snprintf(szPort, sizeof(szPort)/sizeof(szPort[0]), "%d", s_iPort);
   gst_rtsp_server_set_address(pServer, "0.0.0.0");
   gst_rtsp_server_set_service(pServer, szPort);

   GstRTSPMountPoints* pMounts = gst_rtsp_server_get_mount_points(pServer);

   GstRTSPMediaFactory* pFactory = gst_rtsp_media_factory_new();
   // The video is passed through as is (no decoding, no reencoding). The
   // source is a pushfilesrc (not a plain filesrc): it timestamps the
   // buffers as they arrive from the named pipe, like a live source does,
   // which is required for a proper RTP packetization. The parsers
   // re-insert the SPS/PPS/VPS headers periodically (config-interval=-1) so
   // that a client can join at any time, without waiting for a full
   // keyframe from the vehicle.
   if ( s_bUseH265 )
      gst_rtsp_media_factory_set_launch(pFactory,
         "pushfilesrc location=" FIFO_RUBY_STATION_VIDEO_STREAM_RTSP " ! h265parse ! rtph265pay name=pay0 pt=96 config-interval=-1");
   else
      gst_rtsp_media_factory_set_launch(pFactory,
         "pushfilesrc location=" FIFO_RUBY_STATION_VIDEO_STREAM_RTSP " ! h264parse ! rtph264pay name=pay0 pt=96 config-interval=-1");

   // A single media pipeline is shared by all the connected clients.
   gst_rtsp_media_factory_set_shared(pFactory, TRUE);
   // Keep the pipeline alive when the last client disconnects, so a
   // reconnecting client does not have to rebuild and preroll it again.
   gst_rtsp_media_factory_set_stop_on_disconnect(pFactory, FALSE);

   // Log client connects and watch the pipeline messages (errors, EOS).
   g_signal_connect(pFactory, "media-configure", G_CALLBACK(_on_media_configure), NULL);
   g_signal_connect(pFactory, "connect", G_CALLBACK(_on_client_connect), NULL);

   gst_rtsp_mount_points_add_factory(pMounts, "/video", pFactory);
   g_object_unref(pMounts);

   guint id = gst_rtsp_server_attach(pServer, NULL);
   if ( 0 == id )
   {
      log_error_and_alarm("[RTSPServer] Failed to attach the RTSP server on port %s. Is the port already in use?", szPort);
      return -1;
   }
   log_line("[RTSPServer] Attached, listening on port %s, mount point: /video", szPort);
   log_line("[RTSPServer] Connect with: rtsp://<this computer ip>:%s/video", szPort);

   GMainLoop* pLoop = g_main_loop_new(NULL, FALSE);
   s_pMainLoop = pLoop;
   // Exit cleanly on SIGTERM/SIGINT (the station kills the process when it
   // must be restarted, for example on a video codec change).
   g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(_handle_signal), NULL);
   g_unix_signal_add(SIGINT, G_SOURCE_FUNC(_handle_signal), NULL);
   g_main_loop_run(pLoop);

   log_line("[RTSPServer] Finished. RTSP server stopped.");
   g_main_loop_unref(pLoop);
   g_object_unref(pServer);
   return 0;
}
