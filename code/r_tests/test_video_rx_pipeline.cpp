/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
        * Neither the name of the organization nor
        the names of its contributors may be used to endorse or promote products
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

// Video RX pipeline regression test.
//
// This test drives the REAL receive-side video code of the station:
//
//   process_received_video_component_packet()   (code/r_station/process_video_packets.cpp)
//     -> ProcessorRxVideo::handleReceivedVideoPacket()
//       -> VideoRxPacketsBuffer::checkAddVideoPacket()
//       -> ProcessorRxVideo::_checkAndOutputAvailablePackets()
//         -> ProcessorRxVideo::processAndOutputVideoPacket()
//           -> rx_video_output_video_data()
//             -> _rx_video_output_to_video_streamer_pipe()  (the FIFO the desktop player reads)
//             -> rx_video_recording_on_new_data()           (the recording file)
//
// Nothing in the production code is modified for this test. The only seam is
// the FIFO and the recording file, which are the real production sinks: the
// test asserts on the bytes that actually reach them.
//
// The test does not need any radio hardware, no monitor mode, no root
// privileges and no network access. The video payload is the in-tree
// known-good H.264 clip (res/intro.h264), sliced into real radio video packets
// with the real packet structures and the real CRC code.

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_video.h"
#include "../base/config_obj_names.h"
#include "../base/config_file_names.h"
#include "../base/ctrl_preferences.h"
#include "../base/ctrl_settings.h"
#include "../base/controller_rt_info.h"
#include "../base/flags_video.h"
#include "../base/hardware_procs.h"
#include "../base/hardware_radio.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/shared_mem.h"
#include "../base/shared_mem_controller_only.h"
#include "../common/string_utils.h"
#include "../radio/fec.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../radio/radiopacketsqueue.h"
#include "../r_station/processor_rx_video.h"
#include "../r_station/process_video_packets.h"
#include "../r_station/rx_video_output.h"
#include "../r_station/rx_video_recording.h"
#include "../r_station/shared_vars.h"
#include "../r_station/shared_vars_state.h"
#include "../r_station/timers.h"

#include <pcap.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//---------------------------------------------------------------------------------
// Test-only stubs
//
// The receive-side video code calls a few functions that live in
// ruby_rt_station.cpp, next to its main(). Linking that file into a test is
// not possible (two main() functions), and the functions themselves talk to
// the central process over an IPC pipe that does not exist in the test.
//
// These are the smallest possible test-only seams: they are NOT duplicated
// behaviour (the code under test does not depend on their result for the
// video path), and they avoid changing any production code just to make the
// test link.
//---------------------------------------------------------------------------------

void send_alarm_to_central(u32 uAlarm, u32 uFlags1, u32 uFlags2)
{
   log_line("[TestVideoRx] send_alarm_to_central: alarm %u, flags %u, %u", uAlarm, uFlags1, uFlags2);
}

void log_ipc_send_central_error(u8* pPacket, int iLength)
{
   log_line("[TestVideoRx] log_ipc_send_central_error: %d bytes", iLength);
}

void send_adaptive_video_paused_to_central(u32 uVehicleId, bool bPaused)
{
   log_line("[TestVideoRx] send_adaptive_video_paused_to_central: VID %u, paused: %s", uVehicleId, (bPaused?"yes":"no"));
}

// There is no radio link test running in the test.
bool test_link_is_in_progress()
{
   return false;
}

//---------------------------------------------------------------------------------
// Test-only seam: the queue with the packets that the station sends to the
// vehicle
//
// The queue is defined in ruby_rt_station.cpp, next to its main(), so it
// cannot be linked in this test. The test has its own copy: the station
// video RX code puts the retransmission requests in it and the test reads
// them from it. The bytes are the production bytes: the request is built by
// ProcessorRxVideo::checkAndRequestMissingPackets() and it is queued with
// packets_queue_add_packet_mark_time(), the same call the station uses.
//---------------------------------------------------------------------------------

t_packet_queue s_QueueRadioPacketsHighPrio;

// The router state accessors are defined in ruby_rt_station.cpp, next to its
// main(), so they cannot be linked in this test. The video RX code uses them
// only for the log messages of the retransmission requests, they do not
// change the request that is built.
u32 router_get_last_time_checked_for_video_packets()
{
   return 0;
}

bool router_is_eof()
{
   return false;
}

//---------------------------------------------------------------------------------
// Test-only seam: the state of the video streamer process
//
// These are the two globals the station code uses to keep the state of the
// video streamer process (code/r_station/rx_video_output.cpp). They are used
// here only to keep the desktop video player from being started by the test:
// the player opens the same FIFO for reading, so it would compete with this
// test for the video bytes (a FIFO with two readers splits the stream between
// them) and the byte counts would not be deterministic.
//
// Everything else on the output path is the production code:
// rx_video_output_video_data() -> _rx_video_output_to_video_streamer_pipe()
// -> write() on the FIFO, the same FIFO the desktop player reads.
// No production code is changed for this test.
//---------------------------------------------------------------------------------

extern int s_iPIDVideoStreamer;
extern u8 s_uCurrentReceivedVideoStreamType;

//---------------------------------------------------------------------------------
// Test harness
//---------------------------------------------------------------------------------

static int s_iTests = 0;
static int s_iFailures = 0;

static void _check(const char* szName, int iGot, int iExpected)
{
   s_iTests++;
   if ( iGot == iExpected )
   {
      printf("  PASS  %-62s got=%d\n", szName, iGot);
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-62s got=%d expected=%d\n", szName, iGot, iExpected);
}

static void _check_true(const char* szName, bool bGot, bool bExpected)
{
   s_iTests++;
   if ( bGot == bExpected )
   {
      printf("  PASS  %-62s got=%s\n", szName, (bGot?"yes":"no"));
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-62s got=%s expected=%s\n", szName, (bGot?"yes":"no"), (bExpected?"yes":"no"));
}

//---------------------------------------------------------------------------------
// Test constants
//---------------------------------------------------------------------------------

// The vehicle id used by the test. It is a fixed value so the test is
// deterministic; it does not have to be a real vehicle.
#define TEST_VID 0x12345678

// The video block scheme used by the test: 4 data packets + 1 EC packet.
#define TEST_BLOCK_DATA_PACKETS 4
#define TEST_BLOCK_EC_PACKETS 1

// The size of a whole video block packet, as sent by the vehicle.
// It includes the video segment important header, the same way the TX side
// computes it: uCurrentBlockPacketSize = usable video data + important header.
#define TEST_BLOCK_PACKET_SIZE 1200

// The video data that fits in one video block packet.
#define TEST_USABLE_VIDEO_DATA_SIZE (TEST_BLOCK_PACKET_SIZE - (int)sizeof(t_packet_header_video_segment_important))

// The number of video blocks sent in one test frame.
#define TEST_FRAME_BLOCKS 3

// The number of data packets in one test frame.
#define TEST_FRAME_PACKETS (TEST_FRAME_BLOCKS * TEST_BLOCK_DATA_PACKETS)

// The video resolution announced by the test vehicle in the video stream info.
// It is the resolution of the in-tree test clip (res/intro.h264).
#define TEST_VIDEO_WIDTH 1280
#define TEST_VIDEO_HEIGHT 720

// The number of frames and the interval between them used by the recording
// test. The recording must be long enough and big enough for the production
// post processing to accept it: the station code discards a recording shorter
// than 2 seconds and the video processing discards a file smaller than
// 100000 bytes or shorter than 3 seconds.
// 21 frames of 14024 bytes = 294684 bytes, sent over 4.2 seconds.
#define TEST_RECORDING_FRAMES 21
#define TEST_RECORDING_FRAME_INTERVAL_MS 200

//---------------------------------------------------------------------------------
// Test state
//---------------------------------------------------------------------------------

static u8 s_uTestVideoFrame[TEST_FRAME_PACKETS * TEST_USABLE_VIDEO_DATA_SIZE];
static int s_iTestVideoFrameSize = 0;

// The bytes received on the video streamer FIFO (the desktop player's input).
static u8 s_uFifoReceived[1024*1024];
static int s_iFifoReceivedBytes = 0;

// The FIFO read end, opened by the test (the station opens the write end).
static int s_iTestFifoReadFd = -1;

// The process stats, used by the FIFO writer.
static shared_mem_process_stats s_TestProcessStats;

//---------------------------------------------------------------------------------
// Helpers
//---------------------------------------------------------------------------------

// Reads a file completely in a buffer. Returns the size, 0 on failure.
static int _read_file(const char* szFileName, u8* pBuffer, int iMaxSize)
{
   FILE* fd = fopen(szFileName, "rb");
   if ( NULL == fd )
      return 0;
   int iRead = (int)fread(pBuffer, 1, iMaxSize, fd);
   fclose(fd);
   return iRead;
}

// Extracts the H.264 access units (NAL units) from an Annex-B elementary
// stream. It returns the number of NAL units found and fills the array with
// the offsets and the sizes of each NAL unit (the start code is not included).
static int _split_annexb_nals(const u8* pData, int iSize, int* pOffsets, int* pSizes, int iMaxNals)
{
   int iCount = 0;
   int iPos = 0;
   while ( iPos + 3 < iSize )
   {
      if ( pData[iPos] == 0 && pData[iPos+1] == 0 && pData[iPos+2] == 1 )
      {
         int iStart = iPos + 3;
         // Find the next start code
         int iNext = iStart;
         while ( iNext + 3 < iSize )
         {
            if ( pData[iNext] == 0 && pData[iNext+1] == 0 && pData[iNext+2] == 1 )
               break;
            iNext++;
         }
         int iEnd = iNext;
         // Trim the trailing zero of the next start code
         while ( iEnd > iStart && pData[iEnd-1] == 0 )
            iEnd--;

         if ( iCount >= iMaxNals )
            break;
         pOffsets[iCount] = iStart;
         pSizes[iCount] = iEnd - iStart;
         iCount++;
         iPos = iNext;
         continue;
      }
      iPos++;
   }
   return iCount;
}

// Same as _split_annexb_nals(), but for the 4 byte start codes (00 00 00 01).
// The empty NAL units (a start code immediately followed by another start
// code) are skipped. The recorded stream starts with a 4 byte start code
// written by the station code, followed by the received stream, so the first
// NAL unit of the recorded stream is the first NAL unit of the stream.
static int _split_annexb_nals4(const u8* pData, int iSize, int* pOffsets, int* pSizes, int iMaxNals)
{
   int iCount = 0;
   int iPos = 0;
   while ( iPos + 4 <= iSize )
   {
      if ( pData[iPos] == 0 && pData[iPos+1] == 0 && pData[iPos+2] == 0 && pData[iPos+3] == 1 )
      {
         int iStart = iPos + 4;
         int iNext = iStart;
         while ( iNext + 4 <= iSize )
         {
            if ( pData[iNext] == 0 && pData[iNext+1] == 0 && pData[iNext+2] == 0 && pData[iNext+3] == 1 )
               break;
            iNext++;
         }
         int iEnd = iNext;
         while ( iEnd > iStart && pData[iEnd-1] == 0 )
            iEnd--;

         if ( (iEnd > iStart) && (iCount < iMaxNals) )
         {
            pOffsets[iCount] = iStart;
            pSizes[iCount] = iEnd - iStart;
            iCount++;
         }
         iPos = iNext;
         continue;
      }
      iPos++;
   }
   return iCount;
}

// Runs a shell command and collects its output. It returns false if the
// command could not be run.
static bool _run_command(const char* szCommand, char* pOutput, int iMaxSize)
{
   pOutput[0] = 0;
   FILE* fd = popen(szCommand, "r");
   if ( NULL == fd )
      return false;
   int iRead = (int)fread(pOutput, 1, iMaxSize-1, fd);
   pOutput[iRead] = 0;
   pclose(fd);
   return true;
}

// Searches in a list of file names (one per line) for a file with the given
// extension that is not in the previous list. It returns true if such a file
// was found and it copies its name in the result.
static bool _find_new_file(const char* szBefore, const char* szAfter, const char* szExtension, char* pResult, int iMaxSize)
{
   pResult[0] = 0;
   const char* p = szAfter;
   while ( *p )
   {
      const char* pEnd = strchr(p, '\n');
      int iLen = pEnd ? (int)(pEnd - p) : (int)strlen(p);
      if ( (iLen > 0) && (iLen < 512) )
      {
         char szLine[512];
         memcpy(szLine, p, iLen);
         szLine[iLen] = 0;
         int iExtLen = (int)strlen(szExtension);
         if ( (iLen > iExtLen) && (0 == strcmp(szLine + iLen - iExtLen, szExtension)) )
         {
            char szSearch[600];
            char szBeforeNl[9000];
            snprintf(szSearch, sizeof(szSearch), "\n%s\n", szLine);
            snprintf(szBeforeNl, sizeof(szBeforeNl), "\n%s\n", szBefore);
            if ( NULL == strstr(szBeforeNl, szSearch) )
            {
               strcpy(pResult, szLine);
               return true;
            }
         }
      }
      if ( NULL == pEnd )
         break;
      p = pEnd + 1;
   }
   return false;
}

// Builds one video radio packet, exactly as the vehicle side builds it:
//   [t_packet_header][t_packet_header_video_segment]
//   [t_packet_header_video_segment_important][video data]
// It returns the total length of the packet.
static int _build_video_packet_ex(u8* pPacket, int iMaxSize,
      u32 uVehicleId, u32 uStreamPacketIndex,
      u16 uH264FrameIndex, u32 uVideoBlockIndex, u8 uBlockPacketIndex,
      u8 uBlockDataPackets, u8 uBlockECPackets, u16 uBlockPacketSize,
      u16 uFramePacketsInfo, u32 uVideoStatusFlags2,
      const u8* pVideoData, int iVideoDataSize,
      u8 uStreamInfoFlags, u32 uStreamInfo);

static int _build_video_packet(u8* pPacket, int iMaxSize,
      u32 uVehicleId, u32 uStreamPacketIndex,
      u16 uH264FrameIndex, u32 uVideoBlockIndex, u8 uBlockPacketIndex,
      u8 uBlockDataPackets, u8 uBlockECPackets, u16 uBlockPacketSize,
      u16 uFramePacketsInfo, u32 uVideoStatusFlags2,
      const u8* pVideoData, int iVideoDataSize)
{
   return _build_video_packet_ex(pPacket, iMaxSize,
      uVehicleId, uStreamPacketIndex,
      uH264FrameIndex, uVideoBlockIndex, uBlockPacketIndex,
      uBlockDataPackets, uBlockECPackets, uBlockPacketSize,
      uFramePacketsInfo, uVideoStatusFlags2,
      pVideoData, iVideoDataSize,
      VIDEO_STREAM_INFO_FLAG_NONE, 0);
}

// Same as _build_video_packet(), but it can also set the video stream info
// (the video size, the FPS, ...), the same way the vehicle side does: the
// stream info is sent in one packet out of the stream info sequence.
static int _build_video_packet_ex(u8* pPacket, int iMaxSize,
      u32 uVehicleId, u32 uStreamPacketIndex,
      u16 uH264FrameIndex, u32 uVideoBlockIndex, u8 uBlockPacketIndex,
      u8 uBlockDataPackets, u8 uBlockECPackets, u16 uBlockPacketSize,
      u16 uFramePacketsInfo, u32 uVideoStatusFlags2,
      const u8* pVideoData, int iVideoDataSize,
      u8 uStreamInfoFlags, u32 uStreamInfo)
{
   t_packet_header* pPH = (t_packet_header*)pPacket;
   radio_packet_init(pPH, PACKET_COMPONENT_VIDEO, PACKET_TYPE_VIDEO_DATA, STREAM_ID_VIDEO_1);
   pPH->vehicle_id_src = uVehicleId;
   pPH->vehicle_id_dest = 0;
   pPH->stream_packet_idx = (STREAM_ID_VIDEO_1 << PACKET_FLAGS_MASK_SHIFT_STREAM_INDEX) |
        (uStreamPacketIndex & PACKET_FLAGS_MASK_STREAM_PACKET_IDX);
   pPH->radio_link_packet_index = (u16)uStreamPacketIndex;

   int iTotal = (int)(sizeof(t_packet_header) + sizeof(t_packet_header_video_segment) +
        sizeof(t_packet_header_video_segment_important) + iVideoDataSize);
   if ( iTotal > iMaxSize )
      return 0;
   pPH->total_length = (u16)iTotal;

   t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(pPacket + sizeof(t_packet_header));
   memset(pPHVS, 0, sizeof(t_packet_header_video_segment));
   // bits 0..3: video stream index, bits 4..7: codec type.
   // This is the exact layout used by the vehicle side (code/r_vehicle/video_tx_buffers.cpp:
   // m_PacketHeaderVideo.uVideoStreamIndexAndType = 0 | (VIDEO_TYPE_H264<<4)).
   // The RX side reads the codec from the high nibble
   // (code/r_station/rx_video_output.cpp: uVideoStreamType = (pPHVS->uVideoStreamIndexAndType >> 4) & 0x0F).
   pPHVS->uVideoStreamIndexAndType = (u8)((VIDEO_TYPE_H264 << 4) | 0);
   pPHVS->uVideoStatusFlags2 = uVideoStatusFlags2;
   pPHVS->uStreamInfoFlags = uStreamInfoFlags;
   pPHVS->uStreamInfo = uStreamInfo;
   pPHVS->uCurrentVideoLinkProfile = VIDEO_PROFILE_HIGH_QUALITY;
   pPHVS->uCurrentVideoKeyframeIntervalMs = 1000;
   pPHVS->uCurrentVideoBitrateBPS = 2000000;
   pPHVS->uCurrentBlockIndex = uVideoBlockIndex;
   pPHVS->uCurrentBlockPacketIndex = uBlockPacketIndex;
   pPHVS->uCurrentBlockPacketSize = uBlockPacketSize;
   pPHVS->uCurrentBlockDataPackets = uBlockDataPackets;
   pPHVS->uCurrentBlockECPackets = uBlockECPackets;
   pPHVS->uH264FrameIndex = uH264FrameIndex;
   pPHVS->uRuntimeMetrics = 0;
   // low byte: the packet index in the frame, high byte: the total packets in the frame
   pPHVS->uFramePacketsInfo = uFramePacketsInfo;
   pPHVS->uDummy2 = 0;

   t_packet_header_video_segment_important* pPHVSImp =
        (t_packet_header_video_segment_important*)(pPacket + sizeof(t_packet_header) + sizeof(t_packet_header_video_segment));
   pPHVSImp->uVideoDataLength = (u16)iVideoDataSize;
   pPHVSImp->uVideoImportantFlags = 0;

   memcpy(pPacket + sizeof(t_packet_header) + sizeof(t_packet_header_video_segment) +
        sizeof(t_packet_header_video_segment_important), pVideoData, iVideoDataSize);

   radio_packet_compute_crc(pPacket, iTotal);
   return iTotal;
}

// Sends a video packet to the real RX code, exactly as the radio RX thread
// does: the packet is handed over to process_received_video_component_packet().
static void _feed_video_packet(u8* pPacket, int iLength)
{
   // The radio RX thread checks the CRC before handing the packet over.
   if ( ! radio_packet_check_crc(pPacket, iLength) )
   {
      log_line("[TestVideoRx] Dropping a packet with a bad CRC.");
      return;
   }
   process_received_video_component_packet(0, pPacket, iLength);
}

// Drains the video streamer FIFO (the desktop player's input) into the test
// buffer. It returns the number of bytes read.
static int _drain_fifo(u8* pBuffer, int iMaxSize)
{
   int iTotal = 0;
   while ( iTotal < iMaxSize )
   {
      int iRead = (int)read(s_iTestFifoReadFd, pBuffer + iTotal, iMaxSize - iTotal);
      if ( iRead <= 0 )
         break;
      iTotal += iRead;
   }
   return iTotal;
}

// Discards the bytes left in the FIFO by the previous test case, so the
// byte counts of a test case are the bytes of that test case only.
static void _reset_fifo()
{
   while ( _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived)) > 0 )
      ;
   s_iFifoReceivedBytes = 0;
}

//---------------------------------------------------------------------------------
// Test environment
//---------------------------------------------------------------------------------

static void _test_setup_environment()
{
   log_init_local_only("TestVideoRx");

   g_TimeStart = get_current_timestamp_ms();
   g_TimeNow = g_TimeStart;

   // The station is not searching for vehicles and is not updating.
   g_bSearching = false;
   g_bUpdateInProgress = false;
   g_bQuit = false;

   // The controller settings are needed by the video RX code. The defaults are
   // used, with the streamer output set to the FIFO (pipe) mode, so the test
   // does not depend on a saved configuration.
   load_ControllerSettings();
   g_pControllerSettings = get_ControllerSettings();
   if ( NULL != g_pControllerSettings )
   {
      g_pControllerSettings->iStreamerOutputMode = 1;  // 1 = pipe (FIFO)
      g_pControllerSettings->iEnableDebugStats = 0;
      g_pControllerSettings->iDeveloperMode = 0;
      g_pControllerSettings->iWaitFullFrameForOutput = 0;
      g_pControllerSettings->iRecordSTR = 0;
      g_pControllerSettings->iRecordOSD = 0;
   }

   load_Preferences();

   // The process stats are used by the FIFO writer.
   memset(&s_TestProcessStats, 0, sizeof(shared_mem_process_stats));
   g_pProcessStats = &s_TestProcessStats;

   // There is no central process to send the alarms to.
   g_fIPCToCentral = -1;

   // The controller runtime info is used by the video RX code.
   controller_rt_info_init(&g_SMControllerRTInfo);
   controller_debug_video_rt_info_init(&g_SMControllerDebugVideoRTInfo);
   memset(&g_SM_VideoDecodeStats, 0, sizeof(g_SM_VideoDecodeStats));

   // The video RX processors list.
   ProcessorRxVideo::oneTimeInit();

   // The vehicle runtime info: the video is accepted for a paired vehicle.
   memset(&g_State, 0, sizeof(g_State));
   g_State.vehiclesRuntimeInfo[0].uVehicleId = TEST_VID;
   g_State.vehiclesRuntimeInfo[0].bIsPairingDone = true;
   g_State.vehiclesRuntimeInfo[0].bReceivedAnyData = true;
   g_State.vehiclesRuntimeInfo[0].bIsDoingRetransmissions = false;

   // The model of the vehicle.
   // The models list is loaded first (it is needed by findModelWithId2(),
   // which checks the current model first, then the controller models list).
   // loadAllModels() only reads the configuration files, it does not write.
   loadAllModels();

   // The current model is the model of the test vehicle. It is configured in
   // memory only: the test never saves it, so no model file is written.
   Model* pModel = getCurrentModel();
   pModel->resetToDefaults(false);
   pModel->uVehicleId = TEST_VID;
   // The SW version must be at least 11.6 for the video packets to be
   // accepted by the RX code.
   pModel->sw_version = (11 * 256) + 6;
   pModel->is_spectator = false;
   pModel->video_params.iVideoWidth = TEST_VIDEO_WIDTH;
   pModel->video_params.iVideoHeight = TEST_VIDEO_HEIGHT;
   pModel->video_params.iVideoFPS = 25;
   pModel->video_params.iCurrentVideoProfile = VIDEO_PROFILE_HIGH_QUALITY;
   // No retransmissions and no one-way-fixed-video flag: the test wants the
   // "skip missing" behaviour (the incomplete blocks are dropped).
   pModel->video_link_profiles[VIDEO_PROFILE_HIGH_QUALITY].uProfileEncodingFlags = 0;
   g_pCurrentModel = pModel;
}

// Opens the read end of the video streamer FIFO. The station opens the write
// end, the player (or this test) opens the read end.
static void _test_open_fifo()
{
   unlink(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY);
   if ( mkfifo(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, 0666) < 0 )
      log_softerror_and_alarm("[TestVideoRx] Failed to create the video streamer FIFO: %s", strerror(errno));
   s_iTestFifoReadFd = open(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, O_RDONLY | O_NONBLOCK);
   log_line("[TestVideoRx] Opened the video streamer FIFO read end, fd: %d", s_iTestFifoReadFd);
}

// Enables the video output to the streamer, without starting the desktop
// video player.
//
// The station code starts the player (ruby_player_desktop) when the streamer
// output is enabled. The player opens the same FIFO for reading, so it would
// compete with this test for the video bytes. The test declares the streamer
// as already started and sets the expected codec type, the same way the
// station does when it starts the player. The write side of the FIFO is
// still the production code.
static void _test_enable_streamer_output()
{
   s_iPIDVideoStreamer = 10;   // >= 10: a streamer process is considered running
   s_uCurrentReceivedVideoStreamType = VIDEO_TYPE_H264;
   rx_video_output_enable_streamer_output();
}

//---------------------------------------------------------------------------------
// Test 1: the packet structures and the packet builder
//---------------------------------------------------------------------------------

static void _test_packet_builder()
{
   printf("\n--- Test 1: the video radio packet builder ---\n");

   u8 uPacket[MAX_PACKET_TOTAL_SIZE];
   memset(uPacket, 0, sizeof(uPacket));

   u8 uVideoData[100];
   memset(uVideoData, 0xAA, sizeof(uVideoData));

   int iLen = _build_video_packet(uPacket, sizeof(uPacket),
         TEST_VID, 7, 3, 11, 2,
         TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
         (u16)((4 << 8) | 2),
         VIDEO_STATUS_FLAGS2_IS_NAL_START | VIDEO_STATUS_FLAGS2_IS_NAL_I,
         uVideoData, (int)sizeof(uVideoData));

   int iExpected = (int)(sizeof(t_packet_header) + sizeof(t_packet_header_video_segment) +
        sizeof(t_packet_header_video_segment_important) + sizeof(uVideoData));
   _check("the built packet has the expected length", iLen, iExpected);

   t_packet_header* pPH = (t_packet_header*)uPacket;
   _check("the packet is a video data packet", pPH->packet_type, PACKET_TYPE_VIDEO_DATA);
   _check("the packet is in the video component", pPH->packet_flags & PACKET_FLAGS_MASK_MODULE, PACKET_COMPONENT_VIDEO);
   _check("the packet source is the test vehicle", (int)pPH->vehicle_id_src, (int)TEST_VID);
   _check("the packet total length is correct", pPH->total_length, iExpected);
   _check_true("the packet CRC is valid", radio_packet_check_crc(uPacket, iLen) != 0, true);

   t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(uPacket + sizeof(t_packet_header));
   _check("the video codec is H264", (pPHVS->uVideoStreamIndexAndType >> 4) & 0x0F, VIDEO_TYPE_H264);
   _check("the video block index is preserved", pPHVS->uCurrentBlockIndex, 11);
   _check("the video block packet index is preserved", pPHVS->uCurrentBlockPacketIndex, 2);
   _check("the video block data packets count is preserved", pPHVS->uCurrentBlockDataPackets, TEST_BLOCK_DATA_PACKETS);
   _check("the video block EC packets count is preserved", pPHVS->uCurrentBlockECPackets, TEST_BLOCK_EC_PACKETS);
   _check("the video block packet size is preserved", pPHVS->uCurrentBlockPacketSize, TEST_BLOCK_PACKET_SIZE);
   _check("the H264 frame index is preserved", pPHVS->uH264FrameIndex, 3);
   _check("the frame packets info is preserved", pPHVS->uFramePacketsInfo, (4 << 8) | 2);
   _check_true("the NAL start flag is set", (pPHVS->uVideoStatusFlags2 & VIDEO_STATUS_FLAGS2_IS_NAL_START) != 0, true);
   _check_true("the NAL I flag is set", (pPHVS->uVideoStatusFlags2 & VIDEO_STATUS_FLAGS2_IS_NAL_I) != 0, true);

   t_packet_header_video_segment_important* pPHVSImp =
        (t_packet_header_video_segment_important*)(uPacket + sizeof(t_packet_header) + sizeof(t_packet_header_video_segment));
   _check("the video data length is preserved", pPHVSImp->uVideoDataLength, (int)sizeof(uVideoData));

   u8 uCopy[100];
   memcpy(uCopy, uPacket + sizeof(t_packet_header) + sizeof(t_packet_header_video_segment) +
        sizeof(t_packet_header_video_segment_important), sizeof(uVideoData));
   _check_true("the video data is preserved", 0 == memcmp(uCopy, uVideoData, sizeof(uVideoData)), true);

   // A packet with a corrupted CRC must be detected
   uPacket[sizeof(u32)] ^= 0xFF;
   _check_true("a corrupted packet fails the CRC check", radio_packet_check_crc(uPacket, iLen) != 0, false);
}

//---------------------------------------------------------------------------------
// Test 2: the H.264 payload
//---------------------------------------------------------------------------------

static int _test_load_video_payload()
{
   printf("\n--- Test 2: the H.264 test payload ---\n");

   static u8 uClip[1024*1024];
   int iClipSize = _read_file("res/intro.h264", uClip, sizeof(uClip));
   _check_true("the in-tree H.264 clip was read", iClipSize > 0, true);
   if ( iClipSize <= 0 )
      return 0;

   static int iNalOffsets[4096];
   static int iNalSizes[4096];
   int iNals = _split_annexb_nals(uClip, iClipSize, iNalOffsets, iNalSizes, 4096);
   _check_true("the H.264 clip has NAL units", iNals > 0, true);
   if ( iNals <= 0 )
      return 0;

   // The in-tree clip starts with an Access Unit Delimiter (NAL type 9),
   // followed by the SPS (type 7) and the PPS (type 8). The SPS is the NAL that
   // carries the stream profile and level, so the RX parser needs it.
   int iFirstNalType = uClip[iNalOffsets[0]] & 0x1F;
   _check("the first NAL of the clip is an AUD", iFirstNalType, 9);
   if ( iNals > 1 )
      _check("the second NAL of the clip is an SPS", uClip[iNalOffsets[1]] & 0x1F, 7);
   if ( iNals > 2 )
      _check("the third NAL of the clip is a PPS", uClip[iNalOffsets[2]] & 0x1F, 8);

   // Build the test video frame: the first NAL units of the clip, up to the
   // size the test video frame can hold. The Annex-B start codes are kept, the
   // frame is a valid H.264 elementary stream.
   int iNeeded = TEST_FRAME_PACKETS * TEST_USABLE_VIDEO_DATA_SIZE;
   int iUsed = 0;
   for( int i=0; i<iNals; i++ )
   {
      if ( iUsed + iNalSizes[i] + 4 > iNeeded )
         break;
      // The start code (4 bytes) in front of every NAL unit
      s_uTestVideoFrame[iUsed] = 0;
      s_uTestVideoFrame[iUsed+1] = 0;
      s_uTestVideoFrame[iUsed+2] = 0;
      s_uTestVideoFrame[iUsed+3] = 1;
      iUsed += 4;
      memcpy(s_uTestVideoFrame + iUsed, uClip + iNalOffsets[i], iNalSizes[i]);
      iUsed += iNalSizes[i];
   }
   s_iTestVideoFrameSize = iUsed;
   _check_true("the test video frame was built", s_iTestVideoFrameSize > 0, true);
   _check_true("the test video frame is an Annex-B stream",
         (s_uTestVideoFrame[0] == 0 && s_uTestVideoFrame[1] == 0 && s_uTestVideoFrame[2] == 0 && s_uTestVideoFrame[3] == 1), true);

   return iNals;
}

//---------------------------------------------------------------------------------
// Test 3: a complete frame is received, reassembled and delivered
//---------------------------------------------------------------------------------

// Sends one complete video frame (TEST_FRAME_BLOCKS blocks of
// TEST_BLOCK_DATA_PACKETS data packets) to the RX code.
// The first packet of the frame also carries the video stream info with the
// video size, exactly like the vehicle side does (the stream info is sent in
// rotation, one item per packet). Without it the RX code does not know the
// video resolution and the recording path refuses to start.
static int _send_complete_frame(u16 uH264FrameIndex, u32 uVideoBlockIndexStart, u32* pStreamPacketIndex)
{
   int iPacketsSent = 0;
   int iVideoPos = 0;
   int iPacketsInFrame = 0;

   for( int iBlock=0; iBlock<TEST_FRAME_BLOCKS; iBlock++ )
   {
      u32 uVideoBlockIndex = uVideoBlockIndexStart + iBlock;
      for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
      {
         int iDataSize = TEST_USABLE_VIDEO_DATA_SIZE;
         if ( iVideoPos + iDataSize > s_iTestVideoFrameSize )
         {
            iDataSize = s_iTestVideoFrameSize - iVideoPos;
            if ( iDataSize <= 0 )
               break;
         }

         iPacketsInFrame++;
         // The frame packets info: the high byte is the total number of
         // packets in the frame, the low byte is this packet's index in the
         // frame. The vehicle side counts the packets from the end of the
         // frame, the RX side uses it to find the frame packet range.
         u16 uFramePacketsInfo = (u16)(TEST_FRAME_PACKETS << 8);
         uFramePacketsInfo |= (u16)(TEST_FRAME_PACKETS - iPacketsInFrame + 1);

         u32 uVideoStatusFlags2 = 0;
         if ( iPacketsInFrame == 1 )
            uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_START | VIDEO_STATUS_FLAGS2_IS_NAL_I;
         if ( iVideoPos + iDataSize >= s_iTestVideoFrameSize )
            uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_END | VIDEO_STATUS_FLAGS2_IS_END_OF_FRAME;

         u8 uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_NONE;
         u32 uStreamInfo = 0;
         if ( iPacketsInFrame == 1 )
         {
            uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_SIZE;
            uStreamInfo = (u32)((TEST_VIDEO_WIDTH & 0xFFFF) | ((TEST_VIDEO_HEIGHT & 0xFFFF) << 16));
         }

         u8 uPacket[MAX_PACKET_TOTAL_SIZE];
         int iLen = _build_video_packet_ex(uPacket, sizeof(uPacket),
               TEST_VID, (*pStreamPacketIndex)++, uH264FrameIndex, uVideoBlockIndex, (u8)iPacket,
               TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
               uFramePacketsInfo, uVideoStatusFlags2,
               s_uTestVideoFrame + iVideoPos, iDataSize,
               uStreamInfoFlags, uStreamInfo);
         if ( iLen <= 0 )
            break;
         _feed_video_packet(uPacket, iLen);
         iPacketsSent++;

         iVideoPos += iDataSize;
      }
   }
   return iPacketsSent;
}

static void _test_receive_complete_frame()
{
   printf("\n--- Test 3: a complete frame is received, reassembled and delivered ---\n");

   // The video output must be enabled, otherwise the RX code does not output
   // anything to the streamer.
   rx_video_output_init();
   _test_enable_streamer_output();

   u32 uStreamPacketIndex = 1;
   int iPacketsSent = _send_complete_frame(1, 1, &uStreamPacketIndex);
   _check("all the frame packets were sent", iPacketsSent, TEST_FRAME_PACKETS);

   // The RX code outputs the packets as soon as the blocks are complete.
   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   s_iFifoReceivedBytes = iGot;
   _check_true("the video data reached the streamer FIFO", iGot > 0, true);
   _check("the FIFO got the whole test video frame", iGot, s_iTestVideoFrameSize);

   // The delivered data must be exactly the H.264 payload, in order.
   _check_true("the delivered data matches the H.264 payload",
         (iGot == s_iTestVideoFrameSize) && (0 == memcmp(s_uFifoReceived, s_uTestVideoFrame, s_iTestVideoFrameSize)), true);

   // The RX stats must show the outputed packets and blocks.
   _check("the controller stats count the outputed packets",
         (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex],
         TEST_FRAME_PACKETS);
   _check("the controller stats count the outputed blocks",
         (int)g_SMControllerRTInfo.uOutputedVideoBlocks[g_SMControllerRTInfo.iCurrentIndex],
         TEST_FRAME_BLOCKS);

   // The H.264 stream parser must have detected the stream parameters.
   shared_mem_video_stream_stats* pStats = get_shared_mem_video_stream_stats_for_vehicle(&g_SM_VideoDecodeStats, TEST_VID);
   _check_true("the video decode stats have a slot for the vehicle", NULL != pStats, true);
   if ( NULL != pStats )
   {
      // The clip is a Main profile, level 4.1 stream. The parser stores the
      // raw values from the SPS: profile_idc 77 (0x4D) is Main, level_idc 41
      // (0x29) is level 4.1.
      _check("the detected H.264 profile is Main", pStats->uDetectedH264Profile, 77);
      _check("the detected H.264 level is 4.1", pStats->uDetectedH264Level, 41);
   }
}

//---------------------------------------------------------------------------------
// Test 4: a frame is delivered in the right order
//---------------------------------------------------------------------------------

static void _test_receive_order()
{
   printf("\n--- Test 4: the frames are delivered in the right order ---\n");

   _reset_fifo();

   u32 uStreamPacketIndex = 100;
   // Three frames in a row.
   _send_complete_frame(2, 100, &uStreamPacketIndex);
   _send_complete_frame(3, 200, &uStreamPacketIndex);
   _send_complete_frame(4, 300, &uStreamPacketIndex);

   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   _check("the FIFO got the three test frames", iGot, 3 * s_iTestVideoFrameSize);

   // Each frame must be identical to the test payload, in order.
   bool bOrdered = true;
   for( int iFrame=0; iFrame<3; iFrame++ )
   {
      const u8* pFrame = s_uFifoReceived + iFrame * s_iTestVideoFrameSize;
      if ( 0 != memcmp(pFrame, s_uTestVideoFrame, s_iTestVideoFrameSize) )
         bOrdered = false;
   }
   _check_true("the three frames were delivered in order", bOrdered, true);
}

//---------------------------------------------------------------------------------
// Test 5: an incomplete block is dropped
//---------------------------------------------------------------------------------

static void _test_incomplete_block()
{
   printf("\n--- Test 5: an incomplete block is dropped ---\n");

   _reset_fifo();

   u32 uStreamPacketIndex = 1000;
   int iPacketsBefore = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];
   int iSkippedBefore = (int)g_SMControllerRTInfo.uOutputedVideoBlocksSkippedBlocks[g_SMControllerRTInfo.iCurrentIndex];

   // A block with only 2 of the 4 data packets. The next block is complete,
   // so the incomplete bottom block is discarded and only the complete block
   // is output.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS-2; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 10, 1000, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((2 << 8) | (2 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // Now a complete block, with a different block index.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 11, 1001, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   int iPacketsAfter = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];
   int iSkippedAfter = (int)g_SMControllerRTInfo.uOutputedVideoBlocksSkippedBlocks[g_SMControllerRTInfo.iCurrentIndex];

   // With the retransmissions off (a one way link) the RX code outputs the
   // packets of a block as they arrive, so the 2 packets received for the
   // incomplete block are already delivered when the block is discarded. The
   // incomplete block itself is not delivered: it is counted as skipped and
   // the following complete block is delivered completely.
   int iExpectedPackets = (TEST_BLOCK_DATA_PACKETS - 2) + TEST_BLOCK_DATA_PACKETS;
   _check("the packets of the incomplete block and of the complete block were output",
         iPacketsAfter - iPacketsBefore, iExpectedPackets);
   _check("the FIFO got the data of both blocks", iGot, iExpectedPackets * TEST_USABLE_VIDEO_DATA_SIZE);

   // The data delivered for the complete block must be the test payload.
   bool bDataOk = true;
   for( int i=0; i<iExpectedPackets; i++ )
   {
      const u8* pBlock = s_uFifoReceived + i * TEST_USABLE_VIDEO_DATA_SIZE;
      if ( 0 != memcmp(pBlock, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE) )
         bDataOk = false;
   }
   _check_true("the delivered data is the test payload", bDataOk, true);

   _check_true("the incomplete block was counted as skipped", (iSkippedAfter - iSkippedBefore) >= 1, true);
}

//---------------------------------------------------------------------------------
// Test 6: a packet with a bad CRC is rejected
//---------------------------------------------------------------------------------

static void _test_bad_crc()
{
   printf("\n--- Test 6: a packet with a bad CRC is rejected ---\n");

   _reset_fifo();

   u32 uStreamPacketIndex = 2000;
   int iPacketsBefore = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   // A complete block where the first packet has a corrupted CRC. The packet
   // is dropped by the CRC check, so the block stays incomplete and it is not
   // output.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 20, 2000, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen <= 0 )
         continue;
      if ( iPacket == 0 )
         uPacket[sizeof(u32)] ^= 0xFF;  // corrupt the CRC
      _feed_video_packet(uPacket, iLen);
   }

   // Send a following complete block, so the incomplete one is discarded.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 21, 2001, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   int iPacketsAfter = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];
   _check("only the complete block was output", iPacketsAfter - iPacketsBefore, TEST_BLOCK_DATA_PACKETS);
}

//---------------------------------------------------------------------------------
// Test 7: a malformed packet (the block scheme changed) is rejected
//---------------------------------------------------------------------------------

static void _test_malformed_scheme_changed()
{
   printf("\n--- Test 7: a malformed packet (the block scheme changed) is rejected ---\n");

   _reset_fifo();

   u32 uStreamPacketIndex = 3000;
   int iPacketsBefore = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   // Two packets of a block with the standard scheme.
   for( int iPacket=0; iPacket<2; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 30, 3000, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((2 << 8) | (2 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // A third packet of the same block, but with a different block packet
   // size. The RX code detects the scheme change and drops the block.
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 30, 3000, 2,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE + 100,
            (u16)((3 << 8) | 1),
            0, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // A following complete block, with the standard scheme, so the RX state
   // recovers.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 31, 3001, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   int iPacketsAfter = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   // The malformed packet is rejected: the RX code empties the video buffers
   // when the block scheme changes. The 2 packets received before it were
   // already delivered (the retransmissions are off, so the packets are
   // output as they arrive) and the following complete block is delivered
   // completely: the RX state recovers.
   int iExpectedPackets = 2 + TEST_BLOCK_DATA_PACKETS;
   _check("the packets before the malformed one and the complete block after it were output",
         iPacketsAfter - iPacketsBefore, iExpectedPackets);
   _check("the FIFO got the data of both blocks", iGot, iExpectedPackets * TEST_USABLE_VIDEO_DATA_SIZE);

   // The delivered data must be the test payload: the malformed packet
   // contributed nothing to the output.
   bool bDataOk = true;
   for( int i=0; i<iExpectedPackets; i++ )
   {
      const u8* pBlock = s_uFifoReceived + i * TEST_USABLE_VIDEO_DATA_SIZE;
      if ( 0 != memcmp(pBlock, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE) )
         bDataOk = false;
   }
   _check_true("the delivered data is the test payload", bDataOk, true);
}

//---------------------------------------------------------------------------------
// Test 8: a packet for an unknown vehicle is rejected
//---------------------------------------------------------------------------------

static void _test_unknown_vehicle()
{
   printf("\n--- Test 8: a packet for an unknown vehicle is rejected ---\n");

   _reset_fifo();

   int iPacketsBefore = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   u32 uStreamPacketIndex = 4000;
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID + 1, uStreamPacketIndex++, 40, 4000, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - iPacket)),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   int iPacketsAfter = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   _check("no packet was output for the unknown vehicle", iPacketsAfter - iPacketsBefore, 0);
   _check("no data was delivered for the unknown vehicle", iGot, 0);
}

//---------------------------------------------------------------------------------
// Test 9: the EC (FEC) reconstruction of a missing data packet
//---------------------------------------------------------------------------------

static void _test_ec_reconstruction()
{
   printf("\n--- Test 9: a missing data packet is reconstructed with the EC packet ---\n");

   _reset_fifo();

   u32 uStreamPacketIndex = 5000;
   int iPacketsBefore = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   // Build the data packets of a block and the EC packet for it, exactly as
   // the vehicle side does.
   //
   // The FEC is computed over the whole video block packet, starting at the
   // video segment important header (that is where the RX side starts the FEC
   // too: pVideoData points at the important header). The block size is
   // uCurrentBlockPacketSize.
   static u8 uDataPackets[TEST_BLOCK_DATA_PACKETS][TEST_BLOCK_PACKET_SIZE];
   static u8 uECPacket[TEST_BLOCK_PACKET_SIZE];
   for( int i=0; i<TEST_BLOCK_DATA_PACKETS; i++ )
   {
      // The first bytes of each block are the important header, the rest is
      // the video data. The FEC covers both.
      t_packet_header_video_segment_important* pImp =
         (t_packet_header_video_segment_important*)(uDataPackets[i]);
      pImp->uVideoDataLength = TEST_USABLE_VIDEO_DATA_SIZE;
      pImp->uVideoImportantFlags = 0;
      memset(uDataPackets[i] + sizeof(t_packet_header_video_segment_important),
            (i * 37) & 0xFF, TEST_USABLE_VIDEO_DATA_SIZE);
   }

   fec_init();
   unsigned char* pDataPtrs[TEST_BLOCK_DATA_PACKETS];
   unsigned char* pECPtrs[1];
   for( int i=0; i<TEST_BLOCK_DATA_PACKETS; i++ )
      pDataPtrs[i] = uDataPackets[i];
   pECPtrs[0] = uECPacket;
   fec_encode(TEST_BLOCK_PACKET_SIZE, pDataPtrs, TEST_BLOCK_DATA_PACKETS, pECPtrs, 1);

   // Send the data packets 0, 1 and 3 (packet 2 is missing) and the EC packet.
   // The RX code must reconstruct the missing packet with the EC packet.
   for( int i=0; i<TEST_BLOCK_DATA_PACKETS; i++ )
   {
      if ( i == 2 )
         continue;
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 50, 5000, (u8)i,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | (4 - i)),
            (i == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            uDataPackets[i] + sizeof(t_packet_header_video_segment_important), TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }
   {
      // The EC packet is the last packet of the block (its index is the number
      // of the data packets).
      //
      // On the vehicle side the FEC parity is written starting at the video
      // segment important header of the EC packet, and the important header
      // of an EC packet is not overwritten by the header filling code
      // (code/r_vehicle/video_tx_buffers.cpp: _fillVideoPacketHeaders()
      // returns early for the EC packets). So the first bytes of the EC
      // packet are the first bytes of the parity block. The RX side reads
      // the FEC data from the same offset
      // (code/r_station/video_rx_buffers.cpp: pVideoData points at the
      // important header), so the test has to put the parity at the same
      // place, or the reconstruction would use a wrong parity block.
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, 50, 5000, TEST_BLOCK_DATA_PACKETS,
            TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
            (u16)((4 << 8) | 1),
            0, uECPacket + sizeof(t_packet_header_video_segment_important), TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
      {
         memcpy(uPacket + sizeof(t_packet_header) + sizeof(t_packet_header_video_segment),
               uECPacket, sizeof(t_packet_header_video_segment_important));
         radio_packet_compute_crc(uPacket, iLen);
         _feed_video_packet(uPacket, iLen);
      }
   }

   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   int iPacketsAfter = (int)g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex];

   _check("the block was output with all its packets", iPacketsAfter - iPacketsBefore, TEST_BLOCK_DATA_PACKETS);
   _check("the FIFO got the whole reconstructed block", iGot, TEST_BLOCK_DATA_PACKETS * TEST_USABLE_VIDEO_DATA_SIZE);

   // The reconstructed packet (index 2) must have the original data.
   bool bDataOk = true;
   for( int i=0; i<TEST_BLOCK_DATA_PACKETS; i++ )
   {
      const u8* pBlock = s_uFifoReceived + i * TEST_USABLE_VIDEO_DATA_SIZE;
      if ( 0 != memcmp(pBlock, uDataPackets[i] + sizeof(t_packet_header_video_segment_important), TEST_USABLE_VIDEO_DATA_SIZE) )
         bDataOk = false;
   }
   _check_true("the reconstructed packet has the original data", bDataOk, true);

   // The stats must show that the EC was used.
   _check_true("the controller stats count the EC used blocks",
         g_SMControllerRTInfo.uOutputedVideoBlocksECUsed[g_SMControllerRTInfo.iCurrentIndex] > 0, true);
}

//---------------------------------------------------------------------------------
// Test 10: the recording path, independently of the display
//---------------------------------------------------------------------------------

static void _test_recording()
{
   printf("\n--- Test 10: the recording path, independently of the display ---\n");

   // The recording is verified without any display: the video streamer output
   // is disabled, so only the recording file can receive the video.
   rx_video_output_disable_streamer_output();

   hw_execute_bash_command_silent("mkdir -p " FOLDER_RUBY_TEMP, NULL);
   hw_execute_bash_command_silent("mkdir -p " FOLDER_MEDIA, NULL);

   // The list of the media files, to find the file that this recording will
   // produce. The station code moves the recorded file to the media folder
   // when the recording ends.
   static char szMediaBefore[8192];
   szMediaBefore[0] = 0;
   _run_command("ls -1 " FOLDER_MEDIA, szMediaBefore, sizeof(szMediaBefore));

   // The station code opens the recording FIFO with O_CREAT: if an old FIFO
   // with the wrong permissions is in the way, it cannot be opened and the
   // recording cannot start at all. The test recreates it with the normal
   // permissions.
   unlink(FIFO_RUBY_STATION_VIDEO_STREAM_RECORDING);
   if ( mkfifo(FIFO_RUBY_STATION_VIDEO_STREAM_RECORDING, 0666) < 0 )
      log_softerror_and_alarm("[TestVideoRx] Failed to create the video recording FIFO: %s", strerror(errno));

   rx_video_recording_init();

   // The recording must go to the disk: the RAM destination needs a tmpfs
   // mount, which needs root privileges and is not available in the test.
   // (rx_video_recording_init() reloads the preferences, so the destination
   // has to be set after it.)
   get_Preferences()->iVideoDestination = prefVideoDestination_Disk;

   char szRecordingFile[MAX_FILE_PATH_SIZE];
   strcpy(szRecordingFile, FOLDER_RUBY_TEMP);
   strcat(szRecordingFile, FILE_TEMP_VIDEO_FILE);
   unlink(szRecordingFile);

   rx_video_recording_start();

   // Wait for the recording thread to be ready for the data.
   int iWait = 0;
   while ( iWait < 100 )
   {
      if ( rx_video_is_recording() )
         break;
      usleep(20*1000);
      iWait++;
   }
   _check_true("the recording was started", rx_video_is_recording(), true);
   if ( ! rx_video_is_recording() )
      return;

   // The recording thread needs a moment to open the recording file and to
   // start consuming the recording FIFO.
   usleep(500*1000);

   // Send the frames of the recording. The recording must be long enough and
   // big enough for the production post processing to accept it: the station
   // code discards a recording shorter than 2 seconds and the video
   // processing discards a file smaller than 100000 bytes or shorter than
   // 3 seconds.
   u32 uStreamPacketIndex = 6000;
   for( int iFrame=0; iFrame<TEST_RECORDING_FRAMES; iFrame++ )
   {
      _send_complete_frame((u16)(60+iFrame), (u32)(6000 + iFrame*TEST_FRAME_BLOCKS), &uStreamPacketIndex);
      usleep(TEST_RECORDING_FRAME_INTERVAL_MS*1000);
   }

   // Give the recording thread the time to write the data.
   usleep(500*1000);

   // The recording file must exist and must contain the H.264 stream.
   struct stat sFileStat;
   int iHasFile = (0 == stat(szRecordingFile, &sFileStat)) ? 1 : 0;
   _check_true("the recording file was created", iHasFile != 0, true);

   if ( iHasFile )
   {
      _check_true("the recording file has the received data", sFileStat.st_size > 100000, true);

      // The recorded stream must start with a NAL start code and must contain
      // the test payload.
      static u8 uRecorded[1024*1024];
      int iRecordedSize = _read_file(szRecordingFile, uRecorded, sizeof(uRecorded));
      _check_true("the recording file was read", iRecordedSize > 0, true);
      _check_true("the recording starts with a NAL start code",
            (iRecordedSize >= 4) && (uRecorded[0] == 0) && (uRecorded[1] == 0) && (uRecorded[2] == 0) && (uRecorded[3] == 1), true);

      // The recorded stream must contain the test payload.
      bool bFound = false;
      if ( iRecordedSize >= s_iTestVideoFrameSize )
      {
         for( int i=0; i + s_iTestVideoFrameSize <= iRecordedSize; i++ )
         {
            if ( 0 == memcmp(uRecorded + i, s_uTestVideoFrame, s_iTestVideoFrameSize) )
            {
               bFound = true;
               break;
            }
         }
      }
      _check_true("the recording contains the received H.264 payload", bFound, true);

      // The recorded stream must have a valid Annex-B NAL structure.
      static int iOffsets[8192];
      static int iSizes[8192];
      int iNals = _split_annexb_nals4(uRecorded, iRecordedSize, iOffsets, iSizes, 8192);
      _check_true("the recorded stream has NAL units", iNals > 0, true);
      if ( iNals > 0 )
      {
         // The recorded stream starts with the AUD of the test payload, then
         // the SPS and the PPS.
         _check("the first recorded NAL is an AUD", uRecorded[iOffsets[0]] & 0x1F, 9);
         if ( iNals > 1 )
            _check("the second recorded NAL is an SPS", uRecorded[iOffsets[1]] & 0x1F, 7);
         if ( iNals > 2 )
            _check("the third recorded NAL is a PPS", uRecorded[iOffsets[2]] & 0x1F, 8);
      }
   }

   // Stop the recording. The station code writes the pending data to the
   // recording file and then moves the file to the media folder.
   rx_video_recording_stop();
   iWait = 0;
   while ( iWait < 1500 )
   {
      if ( ! rx_video_is_recording() )
         break;
      usleep(20*1000);
      iWait++;
   }
   _check_true("the recording was stopped", ! rx_video_is_recording(), true);

   // The recording must have produced a file in the media folder.
   static char szMediaAfter[8192];
   szMediaAfter[0] = 0;
   _run_command("ls -1 " FOLDER_MEDIA, szMediaAfter, sizeof(szMediaAfter));
   char szArtifactName[512];
   int iHasArtifact = _find_new_file(szMediaBefore, szMediaAfter, ".h264", szArtifactName, sizeof(szArtifactName)) ? 1 : 0;
   _check_true("the recording produced a file in the media folder", iHasArtifact != 0, true);

   if ( iHasArtifact )
   {
      char szArtifact[MAX_FILE_PATH_SIZE];
      strcpy(szArtifact, FOLDER_MEDIA);
      strcat(szArtifact, szArtifactName);
      printf("  INFO  the recorded file: %s\n", szArtifact);

      // The produced file must be a valid H.264 stream. It is checked with
      // ffprobe, the same way the recorded files are checked in the station
      // code. If ffprobe is not installed, the check is skipped (it is not a
      // failure of the RX code).
      char szCommand[1024];
      sprintf(szCommand, "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of default=noprint_wrappers=1 '%s' 2>/dev/null",
         szArtifact);
      char szOutput[1024];
      if ( _run_command(szCommand, szOutput, sizeof(szOutput)) && (strlen(szOutput) > 0) )
      {
         printf("  INFO  ffprobe on the recorded file: %s", szOutput);
         _check_true("the recorded file is an H.264 stream", NULL != strstr(szOutput, "codec_name=h264"), true);
         _check_true("the recorded file has the test resolution", NULL != strstr(szOutput, "width=1280"), true);
         _check_true("the recorded file has the test resolution", NULL != strstr(szOutput, "height=720"), true);
      }
      else
      {
         printf("  INFO  ffprobe is not available, the recorded file was not probed.\n");
      }
   }

   // Enable the streamer output again for the remaining tests.
   _test_enable_streamer_output();
}

//---------------------------------------------------------------------------------
// Test 10: the radio path (the bytes captured over the air to the radio packet)
//---------------------------------------------------------------------------------

// The capture file used by the radio path tests.
#define TEST_CAPTURE_FILE "/tmp/rubyfpv_test_radio_path.pcap"

// The radio interface used by the test. The test replaces the radio entry
// with a fake one: the packets are read from a capture file, so no real
// adapter is ever opened and no network interface is ever touched.
#define TEST_RADIO_INTERFACE 0

// The number of video frames sent in the sustained radio path test.
#define TEST_SUSTAINED_FRAMES 20

// The radio entry replaced by the test. The original one is saved and
// restored, so the rest of the test process sees the real radios again.
static radio_hw_info_t s_oSavedRadioEntry;
static int s_bRadioEntrySaved = 0;

// Prepares the fake radio interface used by the radio path tests.
//
// The packets are read from a pcap capture file, not from a live adapter, so
// the test needs no radio hardware, no monitor mode and no root privileges,
// and it does not change the state of any network interface of the machine.
// The enumeration of the radio interfaces only reads the system information;
// the entry of the test interface is overwritten in memory, in this process
// only.
static int _test_setup_test_radio()
{
   radio_hw_info_t* pRadio = hardware_get_radio_info(TEST_RADIO_INTERFACE);
   if ( NULL == pRadio )
   {
      log_line("[TestVideoRx] No radio interface could be prepared, the radio path tests are skipped.");
      return 0;
   }

   s_oSavedRadioEntry = *pRadio;
   s_bRadioEntrySaved = 1;

   radio_hw_info_t* pRadios = hardware_get_radio_info_array();
   memset(&pRadios[TEST_RADIO_INTERFACE], 0, sizeof(radio_hw_info_t));
   pRadios[TEST_RADIO_INTERFACE].iRadioType = RADIO_TYPE_REALTEK;
   pRadios[TEST_RADIO_INTERFACE].isSupported = 1;
   pRadios[TEST_RADIO_INTERFACE].isEnabled = 1;
   pRadios[TEST_RADIO_INTERFACE].isHighCapacityInterface = 1;
   pRadios[TEST_RADIO_INTERFACE].supportedBands = 0x0F;
   pRadios[TEST_RADIO_INTERFACE].uCurrentFrequencyKhz = 5800000;
   pRadios[TEST_RADIO_INTERFACE].phy_index = 0;
   strcpy(pRadios[TEST_RADIO_INTERFACE].szName, "testvrx0");
   strcpy(pRadios[TEST_RADIO_INTERFACE].szMAC, "001122334455");
   strcpy(pRadios[TEST_RADIO_INTERFACE].szDescription, "RubyFPV test radio");
   return 1;
}

// Restores the real radio entry.
static void _test_restore_test_radio()
{
   if ( ! s_bRadioEntrySaved )
      return;
   radio_hw_info_t* pRadios = hardware_get_radio_info_array();
   pRadios[TEST_RADIO_INTERFACE] = s_oSavedRadioEntry;
   s_bRadioEntrySaved = 0;
}

// Builds the radio packets of one video frame. They are the same packets the
// vehicle side sends, with the same packet structures and the same CRC code,
// but they are kept in an array instead of being handed over to the RX code.
static int _build_frame_radio_packets(u8* pRadioPackets, int* pPacketSizes, int iMaxPackets,
      u16 uH264FrameIndex, u32 uVideoBlockIndexStart, u32* pStreamPacketIndex)
{
   int iPacketsBuilt = 0;
   int iVideoPos = 0;
   int iPacketsInFrame = 0;
   int iOffset = 0;

   for( int iBlock=0; iBlock<TEST_FRAME_BLOCKS; iBlock++ )
   {
      u32 uVideoBlockIndex = uVideoBlockIndexStart + iBlock;
      for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
      {
         if ( iPacketsBuilt >= iMaxPackets )
            return iPacketsBuilt;

         int iDataSize = TEST_USABLE_VIDEO_DATA_SIZE;
         if ( iVideoPos + iDataSize > s_iTestVideoFrameSize )
         {
            iDataSize = s_iTestVideoFrameSize - iVideoPos;
            if ( iDataSize <= 0 )
               break;
         }

         iPacketsInFrame++;
         u16 uFramePacketsInfo = (u16)(TEST_FRAME_PACKETS << 8);
         uFramePacketsInfo |= (u16)(TEST_FRAME_PACKETS - iPacketsInFrame + 1);

         u32 uVideoStatusFlags2 = 0;
         if ( iPacketsInFrame == 1 )
            uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_START | VIDEO_STATUS_FLAGS2_IS_NAL_I;
         if ( iVideoPos + iDataSize >= s_iTestVideoFrameSize )
            uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_END | VIDEO_STATUS_FLAGS2_IS_END_OF_FRAME;

         u8 uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_NONE;
         u32 uStreamInfo = 0;
         if ( iPacketsInFrame == 1 )
         {
            uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_SIZE;
            uStreamInfo = (u32)((TEST_VIDEO_WIDTH & 0xFFFF) | ((TEST_VIDEO_HEIGHT & 0xFFFF) << 16));
         }

         int iLen = _build_video_packet_ex(pRadioPackets + iOffset, MAX_PACKET_TOTAL_SIZE,
               TEST_VID, (*pStreamPacketIndex)++, uH264FrameIndex, uVideoBlockIndex, (u8)iPacket,
               TEST_BLOCK_DATA_PACKETS, TEST_BLOCK_EC_PACKETS, TEST_BLOCK_PACKET_SIZE,
               uFramePacketsInfo, uVideoStatusFlags2,
               s_uTestVideoFrame + iVideoPos, iDataSize,
               uStreamInfoFlags, uStreamInfo);
         if ( iLen <= 0 )
            break;
         pPacketSizes[iPacketsBuilt] = iLen;
         iOffset += iLen;
         iPacketsBuilt++;

         iVideoPos += iDataSize;
      }
   }
   return iPacketsBuilt;
}

// Builds the RF frames of a set of radio packets, exactly as the vehicle side
// builds them over the air, and keeps a copy of the bytes that are really on
// the air.
//
// The TX code rewrites the radio link packet index and the CRC of the packet,
// so the bytes on the air are not the bytes of the source packet: the copy is
// the reference the RX test compares against.
static int _build_radio_frames(u8* pFrames, int* pFrameSizes, u8* pOnAirPackets, int* pOnAirSizes,
      u8* pRadioPackets, int* pRadioPacketSizes, int iCountPackets, int iMaxFrames)
{
   int iCountFrames = 0;
   int iOffsetFrames = 0;
   int iOffsetPackets = 0;

   for( int i=0; i<iCountPackets; i++ )
   {
      if ( iCountFrames >= iMaxFrames )
         break;

      int iLen = radio_build_new_raw_ieee_packet(TEST_RADIO_INTERFACE, pFrames + iOffsetFrames,
            pRadioPackets + iOffsetPackets, pRadioPacketSizes[i], RADIO_PORT_ROUTER_DOWNLINK, 0);
      if ( iLen <= 0 )
         break;

      // The radio packet is the last part of the frame: the radiotap header
      // and the 802.11 header are in front of it.
      int iHeaderLength = iLen - pRadioPacketSizes[i];
      if ( iHeaderLength < 0 )
         break;

      pFrameSizes[iCountFrames] = iLen;
      memcpy(pOnAirPackets + iOffsetPackets, pFrames + iOffsetFrames + iHeaderLength, pRadioPacketSizes[i]);
      pOnAirSizes[i] = pRadioPacketSizes[i];

      iCountFrames++;
      iOffsetFrames += iLen;
      iOffsetPackets += pRadioPacketSizes[i];
   }
   return iCountFrames;
}

// Writes a set of RF frames to a pcap capture file. The file has the same
// link type as a monitor mode capture of a real link: DLT_IEEE802_11_RADIO,
// that in libpcap is 127 and means "802.11 plus a radiotap radio header".
// Every record is a radiotap header, an 802.11 header and the radio packet.
static int _write_capture_file(const char* szFileName, const u8* pFrames, const int* pFrameSizes, int iCountFrames)
{
   pcap_t* pPcap = pcap_open_dead(DLT_IEEE802_11_RADIO, 65535);
   if ( NULL == pPcap )
      return 0;

   pcap_dumper_t* pDumper = pcap_dump_open(pPcap, szFileName);
   if ( NULL == pDumper )
   {
      pcap_close(pPcap);
      return 0;
   }

   int iOffset = 0;
   for( int i=0; i<iCountFrames; i++ )
   {
      struct pcap_pkthdr oHeader;
      memset(&oHeader, 0, sizeof(oHeader));
      oHeader.caplen = pFrameSizes[i];
      oHeader.len = pFrameSizes[i];
      pcap_dump((u_char*)pDumper, &oHeader, pFrames + iOffset);
      iOffset += pFrameSizes[i];
   }

   pcap_dump_close(pDumper);
   pcap_close(pPcap);
   return 1;
}

// The radio path test: the video packets are wrapped into RF frames with the
// production TX code, they are written to a capture file and they are read
// back with the production RX code (the same filter, the same encapsulation
// check and the same parsing as for a live adapter).
//
// The bytes that come out of the RX code must be exactly the bytes that were
// on the air.
static void _test_radio_path()
{
   printf("\n--- Test 11: the radio path ---\n");

   if ( ! _test_setup_test_radio() )
   {
      _check_true("the radio path test ran", false, true);
      return;
   }

   // The production TX code adds the radiotap header and the 802.11 header
   // to the radio packet and computes the CRC of the packet.
   radio_init_link_structures();

   static u8 s_uRadioPackets[TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iRadioPacketSizes[TEST_FRAME_PACKETS];
   static u8 s_uOnAirPackets[TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iOnAirSizes[TEST_FRAME_PACKETS];
   u32 uStreamPacketIndex = 1;
   int iCountPackets = _build_frame_radio_packets(s_uRadioPackets, s_iRadioPacketSizes, TEST_FRAME_PACKETS,
         1, 1, &uStreamPacketIndex);
   _check("the radio packets of one frame were built", iCountPackets, TEST_FRAME_PACKETS);
   if ( iCountPackets <= 0 )
      return;

   static u8 s_uRadioFrames[TEST_FRAME_PACKETS * (MAX_PACKET_TOTAL_SIZE + 64)];
   static int s_iRadioFrameSizes[TEST_FRAME_PACKETS];
   int iCountFrames = _build_radio_frames(s_uRadioFrames, s_iRadioFrameSizes, s_uOnAirPackets, s_iOnAirSizes,
         s_uRadioPackets, s_iRadioPacketSizes, iCountPackets, TEST_FRAME_PACKETS);
   _check("the RF frames of one frame were built", iCountFrames, iCountPackets);
   if ( iCountFrames <= 0 )
      return;

   _check_true("the capture file was written",
         _write_capture_file(TEST_CAPTURE_FILE, s_uRadioFrames, s_iRadioFrameSizes, iCountFrames) != 0, true);

   // The capture file is opened with the production RX code.
   int iOpened = radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, TEST_CAPTURE_FILE);
   _check("the capture file was opened for reading", iOpened, 1);
   if ( 0 == iOpened )
      return;

   // The frames are read with the production RX code.
   int iCountRead = 0;
   int iCountIdentical = 0;
   int iCountCrcOk = 0;
   int iOffsetOnAir = 0;
   for( int i=0; i<iCountFrames; i++ )
   {
      int iPayloadLength = 0;
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;
      iCountRead++;

      if ( iPayloadLength == s_iOnAirSizes[i] )
      if ( 0 == memcmp(pPayload, s_uOnAirPackets + iOffsetOnAir, iPayloadLength) )
         iCountIdentical++;
      iOffsetOnAir += s_iOnAirSizes[i];

      // The CRC check of the production RX code.
      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength > 0) && bCRCOk )
         iCountCrcOk++;
   }
   _check("all the RF frames were read by the RX code", iCountRead, iCountFrames);
   _check("the packets read by the RX code are identical to the ones on the air", iCountIdentical, iCountFrames);
   _check("all the packets pass the production CRC check", iCountCrcOk, iCountFrames);

   // The end of the capture file is reached cleanly: no packet, no error.
   int iPayloadLength = 0;
   u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
   _check_true("the end of the capture file is reached cleanly", NULL == pPayload, true);
}

// The sustained radio path test: many video frames go through the radio path
// (the capture file is built with the production TX code and it is read back
// with the production RX code) and the video is reconstructed and delivered to
// the video streamer FIFO, the same way it is delivered when the packets come
// from a real radio link.
//
// It measures the continuity of the stream (every video block must be
// complete), the number of the delivered frames and the time needed to
// process the whole capture.
static void _test_sustained_radio_path()
{
   printf("\n--- Test 12: the sustained radio path ---\n");

   if ( ! _test_setup_test_radio() )
   {
      _check_true("the sustained radio path test ran", false, true);
      return;
   }

   radio_init_link_structures();

   // The radio packets of all the test frames. The frame and the block
   // indexes are far away from the ones used by the other tests, so the RX
   // code does not consider them as old retransmissions.
   const int iMaxPackets = TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS;
   static u8 s_uRadioPackets[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iRadioPacketSizes[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS];
   static u8 s_uOnAirPackets[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iOnAirSizes[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS];
   u32 uStreamPacketIndex = 1;
   int iCountPackets = 0;
   int iOffsetPackets = 0;
   for( int iFrame=0; iFrame<TEST_SUSTAINED_FRAMES; iFrame++ )
   {
      int iCount = _build_frame_radio_packets(s_uRadioPackets + iOffsetPackets,
            s_iRadioPacketSizes + iCountPackets, iMaxPackets - iCountPackets,
            (u16)(1000 + iFrame), (u32)(1000 + iFrame * TEST_FRAME_BLOCKS), &uStreamPacketIndex);
      if ( iCount <= 0 )
         break;
      for( int i=0; i<iCount; i++ )
         iOffsetPackets += s_iRadioPacketSizes[iCountPackets + i];
      iCountPackets += iCount;
   }
   _check("the radio packets of the sustained test were built", iCountPackets, iMaxPackets);
   if ( iCountPackets <= 0 )
      return;

   // The RF frames of all the packets.
   static u8 s_uRadioFrames[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS * (MAX_PACKET_TOTAL_SIZE + 64)];
   static int s_iRadioFrameSizes[TEST_SUSTAINED_FRAMES * TEST_FRAME_PACKETS];
   int iCountFrames = _build_radio_frames(s_uRadioFrames, s_iRadioFrameSizes, s_uOnAirPackets, s_iOnAirSizes,
         s_uRadioPackets, s_iRadioPacketSizes, iCountPackets, iMaxPackets);
   _check("the RF frames of the sustained test were built", iCountFrames, iCountPackets);
   if ( iCountFrames <= 0 )
      return;

   _check_true("the capture file of the sustained test was written",
         _write_capture_file(TEST_CAPTURE_FILE, s_uRadioFrames, s_iRadioFrameSizes, iCountFrames) != 0, true);

   int iOpened = radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, TEST_CAPTURE_FILE);
   _check("the capture file of the sustained test was opened for reading", iOpened, 1);
   if ( 0 == iOpened )
      return;

   // The video output must be enabled, otherwise the RX code does not output
   // anything to the streamer.
   rx_video_output_init();
   _test_enable_streamer_output();
   _reset_fifo();

   u32 uStartTime = get_current_timestamp_ms();
   int iCountRead = 0;
   int iCountCrcOk = 0;
   int iFifoBytes = 0;
   int iCountDelivered = 0;
   int iCountBadFrames = 0;
   for( int iFrame=0; iFrame<TEST_SUSTAINED_FRAMES; iFrame++ )
   {
      // The packets of one video frame.
      for( int i=0; i<TEST_FRAME_PACKETS; i++ )
      {
         int iPayloadLength = 0;
         u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
         if ( NULL == pPayload )
            break;
         iCountRead++;

         int bCRCOk = 0;
         int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
         if ( (iPacketLength <= 0) || ! bCRCOk )
            continue;
         iCountCrcOk++;

         // The packet is handed over to the video RX code, exactly as the
         // radio RX thread does it.
         process_received_video_component_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      }

      // The player reads the FIFO while the video is received: the FIFO has a
      // limited buffer, so the test drains it as a real player would do.
      int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
      iFifoBytes += iGot;
      if ( iGot == s_iTestVideoFrameSize )
      {
         iCountDelivered++;
         if ( 0 != memcmp(s_uFifoReceived, s_uTestVideoFrame, iGot) )
            iCountBadFrames++;
      }
      else if ( iGot > 0 )
      {
         iCountBadFrames++;
      }
   }
   u32 uElapsed = get_current_timestamp_ms() - uStartTime;

   // The bytes that are still in the FIFO.
   iFifoBytes += _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));

   printf("  INFO  %d RF frames read, %d packets with a good CRC, %d bytes delivered to the video FIFO in %u ms\n",
         iCountRead, iCountCrcOk, iFifoBytes, uElapsed);

   _check("all the RF frames of the sustained test were read", iCountRead, iCountFrames);
   _check("all the packets of the sustained test pass the CRC check", iCountCrcOk, iCountFrames);
   _check("the sustained test delivered all the video frames", iCountDelivered, TEST_SUSTAINED_FRAMES);
   _check("the delivered video has the expected size", iFifoBytes, TEST_SUSTAINED_FRAMES * s_iTestVideoFrameSize);
   _check_true("all the delivered video frames are identical to the test payload", iCountBadFrames == 0, true);

   // The real radio interface is restored.
   _test_restore_test_radio();
}

//---------------------------------------------------------------------------------
// Test 12: the station requests the missing video packets
//
// The station asks the vehicle to send again the video packets that it did
// not receive. The request is built by the production code
// (ProcessorRxVideo::checkAndRequestMissingPackets()) and it is put in the
// high priority packets queue, the same queue that the radio TX thread reads.
// The test reads the request from that queue and checks its bytes, field by
// field, against the packet that is missing.
//---------------------------------------------------------------------------------

static void _test_retransmission_request()
{
   printf("\n--- Test 13: the station requests the missing video packets ---\n");

   _reset_fifo();
   packets_queue_init(&s_QueueRadioPacketsHighPrio);

   // The retransmissions are enabled for this test: with them off (the default
   // of the test model) the RX code drops the incomplete blocks instead of
   // keeping them in the buffer to be requested again. The max retransmission
   // window is set too, the same way the models set it: without it the blocks
   // are discarded as being too old before the request is built.
   int iCurrentProfile = g_pCurrentModel->video_params.iCurrentVideoProfile;
   u32 uOldProfileFlags = g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags;
   u32 uOldRadioLinksFlags = g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags;
   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags |=
         VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS |
         ((u32)DEFAULT_VIDEO_RETRANS_MS5_HP << 8);
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags &= ~((u32)MODEL_RADIOLINKS_FLAGS_DOWNLINK_ONLY);

   ProcessorRxVideo* pProcessor = ProcessorRxVideo::getVideoProcessorForVehicleId(TEST_VID, 0);
   if ( NULL == pProcessor )
   {
      _check_true("the video processor of the test vehicle exists", false, true);
      g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
      g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
      return;
   }
   _check_true("the video processor of the test vehicle exists", true, true);

   // The RX buffer is cleared, so the only missing packet in it is the one of
   // this test case.
   pProcessor->resetReceiveState();

   u32 uStreamPacketIndex = 20000;
   u16 uFrameIndex = 20000;
   const u32 uBottomBlockIndex = 20000;
   const u32 uTopBlockIndex = 20001;
   const u8 uMissingPacketIndex = 2;

   // The bottom block: 3 of the 4 data packets. The packet 2 is missing.
   //
   // The low byte of uFramePacketsInfo is the index of the packet in the
   // frame and the high byte is the number of the data packets in the frame,
   // the same way the vehicle side computes them
   // (code/r_vehicle/video_tx_buffers.cpp: the counter starts at the number of
   // the packets in the frame and it is decremented for every data packet).
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      if ( uMissingPacketIndex == iPacket )
         continue;
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, uFrameIndex, uBottomBlockIndex, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, 0, TEST_BLOCK_PACKET_SIZE,
            (u16)((8 << 8) | iPacket),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // The top block: complete, so it must not be requested.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, uFrameIndex, uTopBlockIndex, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, 0, TEST_BLOCK_PACKET_SIZE,
            (u16)((8 << 8) | (4 + iPacket)),
            0, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // The station asks for the missing packet.
   int iRequested = pProcessor->periodicLoopProcessor(g_TimeNow, true);
   _check("the station requested the missing video packet", iRequested, 1);
   _check("the request is in the high priority queue", packets_queue_has_packets(&s_QueueRadioPacketsHighPrio), 1);

   int iRequestLength = 0;
   u8* pRequest = packets_queue_peek_packet(&s_QueueRadioPacketsHighPrio, 0, &iRequestLength);

   // The request has: the packet header, the retransmission id, the video
   // stream index, the flags, the number of the requested packets and one
   // (video block index, video packet index) pair for every requested packet.
   int iExpectedLength = (int)(sizeof(t_packet_header) + sizeof(u32) + 3*sizeof(u8) + sizeof(u32) + sizeof(u8));
   _check("the length of the retransmission request", iRequestLength, iExpectedLength);

   if ( NULL == pRequest )
   {
      _check_true("the retransmission request was built", false, true);
      g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
      g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
      return;
   }
   _check_true("the retransmission request was built", true, true);

   t_packet_header* pPH = (t_packet_header*)pRequest;
   _check("the request is a video retransmission request", pPH->packet_type, PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS);
   _check("the request is in the video component", pPH->packet_flags & PACKET_FLAGS_MASK_MODULE, PACKET_COMPONENT_VIDEO);
   _check_true("the request is a high priority packet", 0 != (pPH->packet_flags & PACKET_FLAGS_BIT_HIGH_PRIORITY), true);
   _check("the request is sent to the test vehicle", pPH->vehicle_id_dest, TEST_VID);
   _check("the length in the request header", pPH->total_length, iExpectedLength);

   u32 uRetrId = 0;
   memcpy(&uRetrId, pRequest + sizeof(t_packet_header), sizeof(u32));
   _check_true("the request has a retransmission id", 0 != uRetrId, true);

   u8 uVideoStreamIndex = 0;
   memcpy(&uVideoStreamIndex, pRequest + sizeof(t_packet_header) + sizeof(u32), sizeof(u8));
   _check("the request is for the video stream of the test", uVideoStreamIndex, 0);

   u8 uFlags = 0;
   memcpy(&uFlags, pRequest + sizeof(t_packet_header) + sizeof(u32) + sizeof(u8), sizeof(u8));
   _check("the request has no re-requested packets flag", uFlags & 0x01, 0);
   _check("the request has no missing frame end flag", (uFlags >> 2) & 0x01, 0);

   u8 uCount = 0;
   memcpy(&uCount, pRequest + sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8), sizeof(u8));
   _check("the request is for one packet", uCount, 1);

   u32 uBlockIndex = 0;
   memcpy(&uBlockIndex, pRequest + sizeof(t_packet_header) + sizeof(u32) + 3*sizeof(u8), sizeof(u32));
   _check("the request is for the block with the missing packet", uBlockIndex, uBottomBlockIndex);

   u8 uPacketIndex = 0;
   memcpy(&uPacketIndex, pRequest + sizeof(t_packet_header) + sizeof(u32) + 3*sizeof(u8) + sizeof(u32), sizeof(u8));
   _check("the request is for the missing packet of the block", uPacketIndex, uMissingPacketIndex);

   // The second request: the same packet is missing, so it is requested again.
   // The request has the "contains re-requested packets" flag and a new
   // retransmission id.
   int iRequestedAgain = pProcessor->periodicLoopProcessor(g_TimeNow, true);
   _check("the missing video packet is requested again", iRequestedAgain, 1);
   _check("the second request is in the high priority queue", packets_queue_has_packets(&s_QueueRadioPacketsHighPrio), 2);

   u8* pRequest2 = packets_queue_peek_packet(&s_QueueRadioPacketsHighPrio, 1, &iRequestLength);
   if ( NULL == pRequest2 )
   {
      _check_true("the second retransmission request was built", false, true);
   }
   else
   {
      _check_true("the second retransmission request was built", true, true);
      _check("the second request has the same length", iRequestLength, iExpectedLength);

      u32 uRetrId2 = 0;
      memcpy(&uRetrId2, pRequest2 + sizeof(t_packet_header), sizeof(u32));
      _check_true("the second request has a new retransmission id", uRetrId2 != uRetrId, true);

      u8 uFlags2 = 0;
      memcpy(&uFlags2, pRequest2 + sizeof(t_packet_header) + sizeof(u32) + sizeof(u8), sizeof(u8));
      _check("the second request has the re-requested packets flag", uFlags2 & 0x01, 0x01);

      u8 uCount2 = 0;
      memcpy(&uCount2, pRequest2 + sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8), sizeof(u8));
      _check("the second request is for one packet", uCount2, 1);
   }

   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
}

//---------------------------------------------------------------------------------
// Test 13: the retransmission loop
//
// The station asks for the missing video packet (test 12), the request is put
// on the air by the production TX code and the VTX answers with the missing
// packet. The answer is the same video packet, marked as a retransmission,
// the same way the vehicle side marks it
// (code/r_vehicle/video_tx_buffers.cpp: _sendPacket()).
//
// The station must accept the answer and complete the video block: the video
// data of the missing packet has to reach the desktop player.
//---------------------------------------------------------------------------------

static void _test_retransmission_loop()
{
   printf("\n--- Test 14: the retransmission loop ---\n");

   _reset_fifo();
   packets_queue_init(&s_QueueRadioPacketsHighPrio);

   // The retransmissions are enabled for this test, with the max
   // retransmission window, the same way the models set them.
   int iCurrentProfile = g_pCurrentModel->video_params.iCurrentVideoProfile;
   u32 uOldProfileFlags = g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags;
   u32 uOldRadioLinksFlags = g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags;
   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags |=
         VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS |
         ((u32)DEFAULT_VIDEO_RETRANS_MS5_HP << 8);
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags &= ~((u32)MODEL_RADIOLINKS_FLAGS_DOWNLINK_ONLY);

   ProcessorRxVideo* pProcessor = ProcessorRxVideo::getVideoProcessorForVehicleId(TEST_VID, 0);
   if ( NULL == pProcessor )
   {
      _check_true("the video processor of the test vehicle exists", false, true);
      g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
      g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
      return;
   }
   _check_true("the video processor of the test vehicle exists", true, true);
   pProcessor->resetReceiveState();

   u32 uStreamPacketIndex = 21000;
   u16 uFrameIndex = 21000;
   const u32 uBottomBlockIndex = 21000;
   const u32 uTopBlockIndex = 21001;
   const u8 uMissingPacketIndex = 2;

   // The bottom block: 3 of the 4 data packets. The packet 2 is missing.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      if ( uMissingPacketIndex == iPacket )
         continue;
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, uFrameIndex, uBottomBlockIndex, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, 0, TEST_BLOCK_PACKET_SIZE,
            (u16)((8 << 8) | iPacket),
            (iPacket == 0)?(VIDEO_STATUS_FLAGS2_IS_NAL_START|VIDEO_STATUS_FLAGS2_IS_NAL_I):0,
            s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // The top block: complete.
   for( int iPacket=0; iPacket<TEST_BLOCK_DATA_PACKETS; iPacket++ )
   {
      u8 uPacket[MAX_PACKET_TOTAL_SIZE];
      int iLen = _build_video_packet(uPacket, sizeof(uPacket),
            TEST_VID, uStreamPacketIndex++, uFrameIndex, uTopBlockIndex, (u8)iPacket,
            TEST_BLOCK_DATA_PACKETS, 0, TEST_BLOCK_PACKET_SIZE,
            (u16)((8 << 8) | (4 + iPacket)),
            0, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE);
      if ( iLen > 0 )
         _feed_video_packet(uPacket, iLen);
   }

   // The station asks for the missing packet.
   int iRequested = pProcessor->periodicLoopProcessor(g_TimeNow, true);
   _check("the station requested the missing video packet", iRequested, 1);

   int iRequestLength = 0;
   u8* pRequest = packets_queue_peek_packet(&s_QueueRadioPacketsHighPrio, 0, &iRequestLength);
   if ( NULL == pRequest )
   {
      _check_true("the retransmission request was built", false, true);
      g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
      g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
      return;
   }
   _check_true("the retransmission request was built", true, true);

   u32 uRetrId = 0;
   memcpy(&uRetrId, pRequest + sizeof(t_packet_header), sizeof(u32));

   // The request goes on the air with the production TX code.
   if ( ! _test_setup_test_radio() )
   {
      _check_true("the retransmission request was put on the air", false, true);
   }
   else
   {
      radio_init_link_structures();
      static u8 s_uRequestFrame[MAX_PACKET_TOTAL_SIZE + 64];
      int iFrameLength = radio_build_new_raw_ieee_packet(TEST_RADIO_INTERFACE, s_uRequestFrame,
            pRequest, iRequestLength, RADIO_PORT_ROUTER_DOWNLINK, 0);
      _check_true("the retransmission request was put on the air", iFrameLength > 0, true);
      if ( iFrameLength > 0 )
      {
         // The radio packet is the last part of the frame: the radiotap header
         // and the 802.11 header are in front of it.
         const u8* pOnAir = s_uRequestFrame + (iFrameLength - iRequestLength);
         // The TX code rewrites the CRC and the radio link packet index, all
         // the other bytes are the bytes of the request.
         bool bSameBytes = (0 == memcmp(pOnAir + 4, pRequest + 4, 10)) &&
                           (0 == memcmp(pOnAir + 16, pRequest + 16, iRequestLength - 16));
         _check_true("the bytes on the air are the bytes of the request", bSameBytes, true);
         _check_true("the packet on the air has a good CRC",
               radio_packet_check_crc((u8*)pOnAir, iRequestLength) != 0, true);
      }
      _test_restore_test_radio();
   }

   // The VTX answers with the missing video packet, marked as a
   // retransmission, the same way the vehicle side marks it
   // (code/r_vehicle/video_tx_buffers.cpp: _sendPacket()): the retransmitted
   // flag, the high priority flag, the retransmission id in the video stream
   // info and a new stream packet index.
   u8 uAnswer[MAX_PACKET_TOTAL_SIZE];
   int iAnswerLength = _build_video_packet_ex(uAnswer, sizeof(uAnswer),
         TEST_VID, 25000, uFrameIndex, uBottomBlockIndex, uMissingPacketIndex,
         TEST_BLOCK_DATA_PACKETS, 0, TEST_BLOCK_PACKET_SIZE,
         (u16)((8 << 8) | uMissingPacketIndex), 0,
         s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE,
         VIDEO_STREAM_INFO_FLAG_RETRANSMISSION_ID, uRetrId);
   _check_true("the VTX answer was built", iAnswerLength > 0, true);
   if ( iAnswerLength > 0 )
   {
      t_packet_header* pPH = (t_packet_header*)uAnswer;
      pPH->packet_flags |= PACKET_FLAGS_BIT_RETRANSMITED | PACKET_FLAGS_BIT_HIGH_PRIORITY;
      radio_packet_compute_crc(uAnswer, iAnswerLength);
      _feed_video_packet(uAnswer, iAnswerLength);
   }

   // The block is complete now, so the whole frame must be delivered to the
   // desktop player.
   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   int iExpectedPackets = 2 * TEST_BLOCK_DATA_PACKETS;
   _check("the FIFO got the whole frame", iGot, iExpectedPackets * TEST_USABLE_VIDEO_DATA_SIZE);

   bool bDataOk = (iGot == iExpectedPackets * TEST_USABLE_VIDEO_DATA_SIZE);
   for( int i=0; bDataOk && (i < iExpectedPackets); i++ )
   {
      const u8* pBlock = s_uFifoReceived + i * TEST_USABLE_VIDEO_DATA_SIZE;
      if ( 0 != memcmp(pBlock, s_uTestVideoFrame, TEST_USABLE_VIDEO_DATA_SIZE) )
         bDataOk = false;
   }
   _check_true("the delivered data is the test payload", bDataOk, true);

   _check_true("the retransmitted packet was counted as retransmitted",
         g_SMControllerRTInfo.uOutputedVideoPacketsRetransmitted[g_SMControllerRTInfo.iCurrentIndex] > 0, true);
   _check_true("the retransmitted packet was not discarded",
         0 == g_SMControllerRTInfo.uOutputedVideoPacketsRetransmittedDiscarded[g_SMControllerRTInfo.iCurrentIndex], true);

   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags = uOldProfileFlags;
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags = uOldRadioLinksFlags;
}

//---------------------------------------------------------------------------------

//---------------------------------------------------------------------------------
// Live feed: the production RX pipeline feeding the desktop video player
//
// The live feed mode sends the real video stream (res/intro.h264) through the
// production receive pipeline, in real time, so that the desktop video player
// displays it. The path is the same one the radio RX thread uses:
//
//   process_received_video_component_packet()
//     -> ProcessorRxVideo::handleReceivedVideoPacket()
//       -> VideoRxPacketsBuffer::checkAddVideoPacket()
//         -> ProcessorRxVideo::_checkAndOutputAvailablePackets()
//           -> rx_video_output_video_data()
//             -> _rx_video_output_to_video_streamer_pipe()  (the FIFO)
//               -> the desktop player (ruby_player_desktop) displays it
//
// The video radio packets are built exactly as the vehicle side builds them
// (the same header structures, the same CRC, the same block scheme) and they
// are handed over to the RX code with the same call the radio RX thread uses.
//
// The difference from the other test cases is only the FIFO: in the live feed
// the FIFO is read by the desktop player, not by the test, so the test does
// not open the read end of the FIFO.
//---------------------------------------------------------------------------------

// The maximum number of the video data bytes in one video radio packet.
#define LIVE_FEED_MAX_VIDEO_DATA 1196

// The number of the data packets in one video block.
#define LIVE_FEED_BLOCK_DATA_PACKETS 4

// The size of a video block packet: the video data plus the important header,
// the same way the vehicle side computes it.
#define LIVE_FEED_BLOCK_PACKET_SIZE (LIVE_FEED_MAX_VIDEO_DATA + (int)sizeof(t_packet_header_video_segment_important))

// The maximum number of the access units of the test clip.
#define LIVE_FEED_MAX_AUS 512

// The frame rate of the test clip. The central application plays the same clip
// at 15 fps (code/r_central/ruby_central.cpp).
#define LIVE_FEED_FPS 15

typedef struct
{
   int iOffset;
   int iSize;
   bool bIsKeyFrame;
} t_live_feed_au;

extern int s_fPipeVideoOutToStreamer;

// Loads the access units of the in-tree H.264 clip (res/intro.h264).
// The access unit data is allocated here and it has to be freed by the caller.
// The access units are the real video frames of the real video stream: they
// are used both by the live feed mode (the packets go into the production RX
// pipeline) and by the RF test mode (the packets go on the air).
static int _test_load_clip_access_units(t_live_feed_au* pAUs, int iMaxAUs, u8** ppAuData)
{
   *ppAuData = NULL;

   int iClipMaxSize = 2*1024*1024;
   u8* pClip = (u8*)malloc(iClipMaxSize);
   if ( NULL == pClip )
      return 0;
   int iClipSize = _read_file("res/intro.h264", pClip, iClipMaxSize);
   if ( iClipSize <= 0 )
   {
      free(pClip);
      return 0;
   }
   printf("  The H.264 clip: %d bytes.\n", iClipSize);

   // The NAL units of the clip.
   static int iNalOffsets[4096];
   static int iNalSizes[4096];
   int iNals = _split_annexb_nals(pClip, iClipSize, iNalOffsets, iNalSizes, 4096);
   if ( iNals <= 0 )
   {
      free(pClip);
      return 0;
   }

   // The access units: an access unit starts with an AUD NAL unit (type 9).
   // The access unit data is the NAL units of the access unit, each of them
   // with its Annex-B start code, so the data that reaches the player is a
   // valid H.264 elementary stream.
   u8* pAuData = (u8*)malloc((size_t)iClipSize + 4096);
   if ( NULL == pAuData )
   {
      free(pClip);
      return 0;
   }
   int iAUs = 0;
   int iAuDataPos = 0;
   for( int i=0; i<iNals; i++ )
   {
      int iNalType = pClip[iNalOffsets[i]] & 0x1F;
      if ( 9 == iNalType )
      {
         // A new access unit starts here.
         if ( iAUs > 0 )
            pAUs[iAUs-1].iSize = iAuDataPos - pAUs[iAUs-1].iOffset;
         if ( iAUs >= iMaxAUs )
            break;
         pAUs[iAUs].iOffset = iAuDataPos;
         pAUs[iAUs].iSize = 0;
         pAUs[iAUs].bIsKeyFrame = false;
         iAUs++;
      }
      if ( iAUs <= 0 )
         continue;

      // The NAL unit, with its start code.
      pAuData[iAuDataPos++] = 0;
      pAuData[iAuDataPos++] = 0;
      pAuData[iAuDataPos++] = 0;
      pAuData[iAuDataPos++] = 1;
      memcpy(pAuData + iAuDataPos, pClip + iNalOffsets[i], iNalSizes[i]);
      iAuDataPos += iNalSizes[i];

      // An IDR NAL unit (type 5) means a key frame.
      if ( 5 == iNalType )
         pAUs[iAUs-1].bIsKeyFrame = true;
   }
   if ( iAUs > 0 )
      pAUs[iAUs-1].iSize = iAuDataPos - pAUs[iAUs-1].iOffset;
   free(pClip);
   printf("  The access units: %d (the first one: %d bytes).\n", iAUs, pAUs[0].iSize);

   *ppAuData = pAuData;
   return iAUs;
}

static int _test_live_feed(int iSeconds)
{
   printf("\n--- Live feed: the RX pipeline feeding the desktop video player ---\n");
   printf("  The video player has to be running: it is the reader of the FIFO.\n");

   // The video stream: the in-tree clip, the same one the offline player uses.
   static t_live_feed_au s_AUs[LIVE_FEED_MAX_AUS];
   u8* pAuData = NULL;
   int iAUs = _test_load_clip_access_units(s_AUs, LIVE_FEED_MAX_AUS, &pAuData);
   if ( iAUs <= 0 )
   {
      printf("  ERROR: the H.264 clip (res/intro.h264) could not be read.\n");
      return -1;
   }

   // The FIFO: it is created here, the desktop player opens the read end and
   // the station code (the video output) opens the write end. The test does
   // not open the read end: it would compete with the player for the video
   // bytes (a FIFO with two readers splits the stream between them).
   // The FIFO is created only if it is not there: the player might have it
   // open already, and replacing it would leave the player reading from an
   // old (deleted) FIFO.
   if ( 0 != access(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, F_OK) )
   {
      if ( mkfifo(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, 0666) < 0 )
      {
         printf("  ERROR: failed to create the video streamer FIFO: %s\n", strerror(errno));
         free(pAuData);
         return -1;
      }
   }

   // The video output to the streamer: the production code opens the write end
   // of the FIFO. The open fails if there is no reader (the player).
   rx_video_output_init();
   _test_enable_streamer_output();
   if ( s_fPipeVideoOutToStreamer < 0 )
   {
      printf("  ERROR: the video output could not open the write end of the FIFO.\n");
      printf("         The desktop video player has to be running (it is the reader).\n");
      free(pAuData);
      return -1;
   }
   printf("  The FIFO write end is open (fd: %d). Starting the video stream.\n", s_fPipeVideoOutToStreamer);

   // The video stream.
   u32 uStreamPacketIndex = 1;
   u16 uFrameIndex = 1;
   u32 uBlockIndex = 1;
   int iAuIndex = 0;
   int iAuSent = 0;
   int iPacketsSent = 0;
   u32 uTimeStart = get_current_timestamp_ms();
   u32 uTimeNextFrame = uTimeStart;

   while ( ! g_bQuit )
   {
      g_TimeNow = get_current_timestamp_ms();
      if ( g_TimeNow - uTimeStart > (u32)(iSeconds * 1000) )
         break;

      t_live_feed_au* pAU = &s_AUs[iAuIndex];
      int iPackets = (pAU->iSize + LIVE_FEED_MAX_VIDEO_DATA - 1) / LIVE_FEED_MAX_VIDEO_DATA;
      if ( iPackets > 255 )
      {
         // The number of the packets in a frame is a byte, so a frame can not
         // have more than 255 packets. The access units of the test clip are
         // much smaller than that.
         printf("  ERROR: the access unit %d has %d packets, it is too big.\n", iAuIndex, iPackets);
         break;
      }

      int iPos = 0;
      int iPacketInAU = 0;
      while ( iPacketInAU < iPackets )
      {
         // The number of the data packets of this block. The last block of an
         // access unit can have less than 4 packets.
         int iBlockPackets = LIVE_FEED_BLOCK_DATA_PACKETS;
         if ( iPacketInAU + iBlockPackets > iPackets )
            iBlockPackets = iPackets - iPacketInAU;

         for( int i=0; i<iBlockPackets; i++ )
         {
            int iDataSize = pAU->iSize - iPos;
            if ( iDataSize > LIVE_FEED_MAX_VIDEO_DATA )
               iDataSize = LIVE_FEED_MAX_VIDEO_DATA;

            u32 uVideoStatusFlags2 = 0;
            if ( 0 == iPacketInAU )
            {
               uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_START;
               uVideoStatusFlags2 |= pAU->bIsKeyFrame ? VIDEO_STATUS_FLAGS2_IS_NAL_I : VIDEO_STATUS_FLAGS2_IS_NAL_P;
            }
            if ( iPacketInAU == iPackets - 1 )
               uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_END | VIDEO_STATUS_FLAGS2_IS_END_OF_FRAME;

            // The video stream info (the video size) is sent in rotation, the
            // same way the vehicle side sends it.
            u8 uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_NONE;
            u32 uStreamInfo = 0;
            if ( (0 == iPacketInAU) && (0 == (iAuSent % 15)) )
            {
               uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_SIZE;
               uStreamInfo = (u32)((TEST_VIDEO_WIDTH & 0xFFFF) | ((TEST_VIDEO_HEIGHT & 0xFFFF) << 16));
            }

            u8 uPacket[MAX_PACKET_TOTAL_SIZE];
            int iLen = _build_video_packet_ex(uPacket, sizeof(uPacket),
                  TEST_VID, uStreamPacketIndex++, uFrameIndex, uBlockIndex, (u8)i,
                  (u8)iBlockPackets, 0, (u16)LIVE_FEED_BLOCK_PACKET_SIZE,
                  (u16)((iPackets << 8) | (iPacketInAU + i)), uVideoStatusFlags2,
                  pAuData + pAU->iOffset + iPos, iDataSize,
                  uStreamInfoFlags, uStreamInfo);
            if ( iLen > 0 )
            {
               _feed_video_packet(uPacket, iLen);
               iPacketsSent++;
            }
            iPos += iDataSize;
         }
         uBlockIndex++;
         iPacketInAU += iBlockPackets;
      }

      iAuSent++;
      iAuIndex++;
      uFrameIndex++;
      if ( iAuIndex >= iAUs )
         iAuIndex = 0;

      // The frame rate of the clip: the access units are sent at the rate the
      // video was recorded at.
      uTimeNextFrame += (1000 / LIVE_FEED_FPS);
      u32 uNow = get_current_timestamp_ms();
      if ( uTimeNextFrame > uNow )
         hardware_sleep_ms(uTimeNextFrame - uNow);
   }

   g_TimeNow = get_current_timestamp_ms();
   printf("  The video stream was sent for %d seconds: %d access units, %d packets.\n",
      iSeconds, iAuSent, iPacketsSent);
   printf("  The outputed video packets: %u, the outputed video blocks: %u.\n",
      g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex],
      g_SMControllerRTInfo.uOutputedVideoBlocks[g_SMControllerRTInfo.iCurrentIndex]);

   free(pAuData);
   return 0;
}

//---------------------------------------------------------------------------------
// Test 15: the video is forwarded to the local video player over UDP
//
// The station can forward the received video to a second player, over UDP on
// the loopback (the port of the local video player). It is a production
// output path (rx_video_output_video_data() ->
// _rx_video_output_to_local_video_player_udp()), independent of the FIFO that
// the desktop player reads, and it is the path used to feed another player
// (a phone, a second ground station) with the same video.
//---------------------------------------------------------------------------------

static void _test_forward_to_local_player_udp()
{
   printf("\n--- Test 15: the video is forwarded to the local player over UDP ---\n");

   // The receiver: a UDP socket on the port of the local video player. The
   // production code opens its own socket and connects to that port.
   int iRxSocket = socket(AF_INET, SOCK_DGRAM, 0);
   if ( iRxSocket < 0 )
   {
      _check_true("the local player UDP socket was created", false, true);
      return;
   }
   _check_true("the local player UDP socket was created", true, true);

   struct sockaddr_in sAddr;
   bzero(&sAddr, sizeof(sAddr));
   sAddr.sin_family = AF_INET;
   sAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
   sAddr.sin_port = htons((unsigned short)DEFAULT_LOCAL_VIDEO_PLAYER_UDP_PORT);
   if ( 0 != bind(iRxSocket, (struct sockaddr*)&sAddr, sizeof(sAddr)) )
   {
      _check_true("the local player UDP port was bound", false, true);
      close(iRxSocket);
      return;
   }
   _check_true("the local player UDP port was bound", true, true);

   // The receive timeout: the test must not hang if the data does not come.
   struct timeval sTimeout;
   sTimeout.tv_sec = 1;
   sTimeout.tv_usec = 0;
   setsockopt(iRxSocket, SOL_SOCKET, SO_RCVTIMEO, &sTimeout, sizeof(sTimeout));

   // The production code opens the output socket.
   rx_video_output_enable_local_player_udp_output();

   // A complete frame goes through the production RX pipeline.
   _reset_fifo();
   u32 uStreamPacketIndex = 30000;
   int iPacketsSent = _send_complete_frame(300, 300, &uStreamPacketIndex);
   _check("the frame packets were sent", iPacketsSent, TEST_FRAME_PACKETS);

   // The datagrams: one per video packet, in order.
   static u8 s_uUdpData[1024*1024];
   int iTotal = 0;
   int iDatagrams = 0;
   while ( iTotal < (int)sizeof(s_uUdpData) )
   {
      int iRead = (int)recv(iRxSocket, s_uUdpData + iTotal, sizeof(s_uUdpData) - iTotal, 0);
      if ( iRead <= 0 )
         break;
      iTotal += iRead;
      iDatagrams++;
   }

   _check("the UDP output got the whole test video frame", iTotal, s_iTestVideoFrameSize);
   _check("the UDP output got one datagram per video packet", iDatagrams, TEST_FRAME_PACKETS);
   _check_true("the forwarded data matches the H.264 payload",
         (iTotal == s_iTestVideoFrameSize) && (0 == memcmp(s_uUdpData, s_uTestVideoFrame, s_iTestVideoFrameSize)), true);

   rx_video_output_disable_local_player_udp_output();
   close(iRxSocket);
}

//---------------------------------------------------------------------------------
// Test 16: the radio path with the radiotap header of a real monitor interface
//
// The radiotap header that a real monitor interface delivers is not the short
// one that the production TX code builds. mac80211 builds its own header for
// every received frame, and its content depends on what the driver reports:
// the timestamp of the frame, the rate (legacy or HT), the number of the
// chains of the card (with the radiotap namespace). The length of that header
// is variable and the RX code has to find the start of the 802.11 header in
// all of those cases.
//
// The test replays the same RF frames with each of those headers, through the
// production BPF filter (the same filter that is used with a live adapter)
// and the production RX parsing, and it checks that the received packets are
// exactly the ones that were on the air.
//
// The link type of the capture file is DLT_IEEE802_11_RADIO, that in libpcap
// is 127 and means "802.11 plus a radiotap radio header": it is the link type
// that every Linux monitor interface delivers.
//---------------------------------------------------------------------------------

// The radiotap headers of a monitor interface that the test builds. They are
// the headers that mac80211 builds, field by field, in the same order and
// with the same alignment.
#define TEST_MONITOR_RADIOTAP_VARIANTS 4

static const char* s_szMonitorRadiotapVariants[TEST_MONITOR_RADIOTAP_VARIANTS] = {
   "the legacy rate, without the timestamp of the frame",
   "the legacy rate, with the timestamp of the frame",
   "the HT rate (the MCS field), without the timestamp",
   "the radiotap namespace (the card reports its chains)"
};

// The length of each of those headers, the one that mac80211 computes for it.
static const int s_iMonitorRadiotapLengths[TEST_MONITOR_RADIOTAP_VARIANTS] = { 18, 26, 21, 24 };

// Builds the radiotap header of a monitor interface, for the given variant.
// The length of the header is the one that mac80211 computes for it.
static int _build_monitor_radiotap_header(int iVariant, u8* pRadiotap, int iMaxLength)
{
   if ( (iVariant < 0) || (iVariant >= TEST_MONITOR_RADIOTAP_VARIANTS) )
      return 0;

   int iLength = s_iMonitorRadiotapLengths[iVariant];
   if ( iMaxLength < iLength )
      return 0;

   memset(pRadiotap, 0, iLength);

   u32 uPresent = 0;
   int iOffset = 8;

   if ( 1 == iVariant )
   {
      // The timestamp of the frame, in microseconds.
      uPresent |= (1u << IEEE80211_RADIOTAP_TSFT);
      unsigned long long uTsft = 1234567890123ULL;
      memcpy(pRadiotap + iOffset, &uTsft, sizeof(uTsft));
      iOffset += 8;
   }
   else if ( 3 == iVariant )
   {
      // The radiotap namespace: the header has a second bitmap, the one of
      // the namespace, and the fields of the namespace follow the fields of
      // the standard part of the header.
      uPresent |= (1u << IEEE80211_RADIOTAP_EXT) | (1u << IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE);
      u32 uNamespacePresent = (1u << IEEE80211_RADIOTAP_ANTENNA) | (1u << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
      memcpy(pRadiotap + 8, &uNamespacePresent, sizeof(uNamespacePresent));
      iOffset = 12;
   }

   // The flags of the frame: no FCS at the end of the frame.
   uPresent |= (1u << IEEE80211_RADIOTAP_FLAGS);
   pRadiotap[iOffset] = 0x00;
   iOffset++;

   // The rate of the frame, in units of 500 kbps: 12 means 6 Mbps. With an
   // HT rate the byte is not a field, it is only the padding of the channel.
   if ( 2 != iVariant )
   {
      uPresent |= (1u << IEEE80211_RADIOTAP_RATE);
      pRadiotap[iOffset] = 0x0c;
   }
   iOffset++;

   // The channel of the frame: 5825 MHz is the channel 149.
   uPresent |= (1u << IEEE80211_RADIOTAP_CHANNEL);
   u16 uFrequency = 5825;
   memcpy(pRadiotap + iOffset, &uFrequency, sizeof(uFrequency));
   iOffset += 2;
   u16 uChannelFlags = 0x0060;
   memcpy(pRadiotap + iOffset, &uChannelFlags, sizeof(uChannelFlags));
   iOffset += 2;

   // The signal level of the antenna.
   uPresent |= (1u << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
   pRadiotap[iOffset] = (u8)(int8_t)(-42);
   iOffset++;

   // The antenna that received the frame: with the radiotap namespace the
   // antenna is in the namespace, not in the standard part of the header.
   if ( 3 != iVariant )
   {
      uPresent |= (1u << IEEE80211_RADIOTAP_ANTENNA);
      pRadiotap[iOffset] = 0;
      iOffset++;
   }

   // The flags of the reception, with the padding to the alignment of 2 bytes.
   uPresent |= (1u << IEEE80211_RADIOTAP_RX_FLAGS);
   if ( iOffset & 1 )
      iOffset++;
   u16 uRxFlags = 0x0000;
   memcpy(pRadiotap + iOffset, &uRxFlags, sizeof(uRxFlags));
   iOffset += 2;

   // The MCS of the frame: only with an HT rate.
   if ( 2 == iVariant )
   {
      uPresent |= (1u << IEEE80211_RADIOTAP_MCS);
      pRadiotap[iOffset] = 0x01;
      pRadiotap[iOffset + 1] = 0x00;
      pRadiotap[iOffset + 2] = 0x05;
      iOffset += 3;
   }

   // With the radiotap namespace, the signal level and the index of the
   // chain of the antenna are in the namespace.
   if ( 3 == iVariant )
   {
      pRadiotap[iOffset] = (u8)(int8_t)(-40);
      pRadiotap[iOffset + 1] = 0;
      iOffset += 2;
   }

   // The header itself: the version, the length and the bitmap of the fields.
   struct ieee80211_radiotap_header* pHeader = (struct ieee80211_radiotap_header*)pRadiotap;
   pHeader->it_version = 0;
   pHeader->it_len = iLength;
   pHeader->it_present = uPresent;

   // The header has to be exactly as long as the fields that it declares.
   return (iOffset == iLength) ? iLength : 0;
}

// Replaces the radiotap header of a frame with the header of a monitor
// interface. The rest of the frame (the 802.11 header and the radio packet)
// is not touched.
static int _replace_radiotap_header(int iVariant, u8* pFrame, int iFrameLength, u8* pOutput, int iOutputMaxLength)
{
   if ( iFrameLength < 8 )
      return 0;

   // The length of the radiotap header is in the header itself.
   int iRadiotapLength = pFrame[2] | (pFrame[3] << 8);
   if ( (iRadiotapLength < 8) || (iRadiotapLength > iFrameLength) )
      return 0;

   int iNewRadiotapLength = _build_monitor_radiotap_header(iVariant, pOutput, iOutputMaxLength);
   if ( iNewRadiotapLength <= 0 )
      return 0;
   if ( iOutputMaxLength < iNewRadiotapLength + (iFrameLength - iRadiotapLength) )
      return 0;

   memcpy(pOutput + iNewRadiotapLength, pFrame + iRadiotapLength, iFrameLength - iRadiotapLength);
   return iNewRadiotapLength + (iFrameLength - iRadiotapLength);
}

static void _test_radio_path_with_monitor_radiotap()
{
   printf("\n--- Test 16: the radio path with the radiotap header of a monitor interface ---\n");

   if ( ! _test_setup_test_radio() )
   {
      _check_true("the radio path test ran", false, true);
      return;
   }

   radio_init_link_structures();

   static u8 s_uRadioPackets[TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iRadioPacketSizes[TEST_FRAME_PACKETS];
   static u8 s_uOnAirPackets[TEST_FRAME_PACKETS * MAX_PACKET_TOTAL_SIZE];
   static int s_iOnAirSizes[TEST_FRAME_PACKETS];
   u32 uStreamPacketIndex = 1;
   int iCountPackets = _build_frame_radio_packets(s_uRadioPackets, s_iRadioPacketSizes, TEST_FRAME_PACKETS,
         1, 1, &uStreamPacketIndex);
   _check("the radio packets of one frame were built", iCountPackets, TEST_FRAME_PACKETS);
   if ( iCountPackets <= 0 )
      return;

   static u8 s_uRadioFrames[TEST_FRAME_PACKETS * (MAX_PACKET_TOTAL_SIZE + 64)];
   static int s_iRadioFrameSizes[TEST_FRAME_PACKETS];
   int iCountFrames = _build_radio_frames(s_uRadioFrames, s_iRadioFrameSizes, s_uOnAirPackets, s_iOnAirSizes,
         s_uRadioPackets, s_iRadioPacketSizes, iCountPackets, TEST_FRAME_PACKETS);
   _check("the RF frames of one frame were built", iCountFrames, iCountPackets);
   if ( iCountFrames <= 0 )
      return;

   static u8 s_uMonitorFrames[TEST_FRAME_PACKETS * (MAX_PACKET_TOTAL_SIZE + 64)];
   static int s_iMonitorFrameSizes[TEST_FRAME_PACKETS];

   for( int iVariant = 0; iVariant < TEST_MONITOR_RADIOTAP_VARIANTS; iVariant++ )
   {
      printf("  The radiotap header of the monitor interface: %s.\n", s_szMonitorRadiotapVariants[iVariant]);

      // Every frame gets the radiotap header of a monitor interface.
      int iCountMonitorFrames = 0;
      int iOffsetFrames = 0;
      int iOffsetMonitorFrames = 0;
      for( int i=0; i<iCountFrames; i++ )
      {
         int iLength = _replace_radiotap_header(iVariant, s_uRadioFrames + iOffsetFrames, s_iRadioFrameSizes[i],
               s_uMonitorFrames + iOffsetMonitorFrames, (int)sizeof(s_uMonitorFrames) - iOffsetMonitorFrames);
         if ( iLength <= 0 )
            break;
         s_iMonitorFrameSizes[iCountMonitorFrames] = iLength;
         iCountMonitorFrames++;
         iOffsetFrames += s_iRadioFrameSizes[i];
         // The frames with the monitor radiotap are longer than the original
         // ones, so they have their own offset.
         iOffsetMonitorFrames += iLength;
      }
      _check("the RF frames with a monitor radiotap header were built", iCountMonitorFrames, iCountFrames);
      if ( iCountMonitorFrames <= 0 )
         return;

      // The radiotap header of the first frame is the one of a monitor interface.
      _check("the radiotap header of the monitor frames is the one of mac80211",
            s_uMonitorFrames[2] | (s_uMonitorFrames[3] << 8),
            s_iMonitorRadiotapLengths[iVariant]);

      _check_true("the capture file of the monitor frames was written",
            _write_capture_file(TEST_CAPTURE_FILE, s_uMonitorFrames, s_iMonitorFrameSizes, iCountMonitorFrames) != 0, true);

      // The capture file is opened with the production RX code: the same
      // encapsulation check and the same filter as for a live adapter.
      int iOpened = radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, TEST_CAPTURE_FILE);
      _check("the capture file with the monitor radiotap was opened for reading", iOpened, 1);
      if ( 0 == iOpened )
         return;

      int iCountRead = 0;
      int iCountIdentical = 0;
      int iCountCrcOk = 0;
      int iOffsetOnAir = 0;
      for( int i=0; i<iCountMonitorFrames; i++ )
      {
         int iPayloadLength = 0;
         u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
         if ( NULL == pPayload )
            break;
         iCountRead++;

         if ( iPayloadLength == s_iOnAirSizes[i] )
         if ( 0 == memcmp(pPayload, s_uOnAirPackets + iOffsetOnAir, iPayloadLength) )
            iCountIdentical++;
         iOffsetOnAir += s_iOnAirSizes[i];

         int bCRCOk = 0;
         int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
         if ( (iPacketLength > 0) && bCRCOk )
            iCountCrcOk++;
      }
      _check("all the RF frames with a monitor radiotap were read by the RX code", iCountRead, iCountMonitorFrames);
      _check("the packets read with a monitor radiotap are identical to the ones on the air", iCountIdentical, iCountMonitorFrames);
      _check("all the packets with a monitor radiotap pass the production CRC check", iCountCrcOk, iCountMonitorFrames);

      // The end of the capture file is reached cleanly.
      int iPayloadLength = 0;
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      _check_true("the end of the capture file with a monitor radiotap is reached cleanly", NULL == pPayload, true);

      radio_close_interface_for_read(TEST_RADIO_INTERFACE);
   }
}

//---------------------------------------------------------------------------------
// The two-radio RF test
//
// These are the two halves of the two-radio RF test:
//
//   --rf-tx <seconds> <interface>   the VTX side: the real video stream is
//        sliced in real radio video packets, they are wrapped in the real RF
//        frames (radiotap + 802.11) and they are put on the air with the
//        production TX code (radio_build_new_raw_ieee_packet() and
//        radio_write_raw_ieee_packet()).
//   --rf-rx <seconds> <interface>   the VRX side: the frames are read with the
//        production RX code (radio_open_interface_for_read(),
//        radio_process_wlan_data_in(), packet_process_and_check()) and they
//        are handed over to the production video pipeline
//        (process_received_video_component_packet()), so the video reaches
//        the desktop player exactly as it would from a real VTX.
//
// The name of the radio interface is set in memory, in this process only: no
// system configuration is read or written and the change is gone when the
// process exits. The interface itself has to be in monitor mode and the
// process needs the CAP_NET_ADMIN and CAP_NET_RAW capabilities.
//---------------------------------------------------------------------------------

// The RF modes run for a long time, so they have to be stoppable with
// Ctrl-C: the handler only sets the quit flag, the mode closes its
// interfaces and its FIFO write end on the way out.
static void _test_signal_handler(int iSignal)
{
   g_bQuit = true;
}

// The interface that carries the network connection of the machine must never
// be used by these modes. It is refused here, in the program, and not only in
// the script that calls it.
static int _test_rf_interface_is_allowed(const char* szInterface)
{
   if ( (NULL == szInterface) || (0 == strlen(szInterface)) )
   {
      printf("  ERROR: the name of the radio interface is missing.\n");
      return 0;
   }
   if ( (0 == strcmp(szInterface, "wlan0")) || (0 == strcmp(szInterface, "lo")) )
   {
      printf("  ERROR: the interface %s is refused: it is not a Ruby radio interface.\n", szInterface);
      return 0;
   }
   return 1;
}

// Sets the name of the radio interface of a radio index. Only the in-memory
// copy of the radio entry is changed.
static int _test_set_radio_interface(int iRadioIndex, const char* szInterface)
{
   if ( ! _test_rf_interface_is_allowed(szInterface) )
      return 0;
   // The production code enumerates the radio interfaces the first time it
   // needs the radio info, and the enumeration rewrites the whole radio array
   // (it loads it from the configuration file). The test radio entry has to
   // be written after that enumeration, otherwise it is overwritten by it and
   // the production code sees the real radio of the index instead of the test
   // one (and refuses it, because the real radio of the index is not a
   // supported Ruby radio).
   hardware_enumerate_radio_interfaces();
   radio_hw_info_t* pRadios = hardware_get_radio_info_array();
   if ( NULL == pRadios )
      return 0;
   if ( (iRadioIndex < 0) || (iRadioIndex >= MAX_RADIO_INTERFACES) )
      return 0;

   radio_hw_info_t* pRadio = &pRadios[iRadioIndex];
   memset(pRadio, 0, sizeof(radio_hw_info_t));
   pRadio->iRadioType = RADIO_TYPE_REALTEK;
   pRadio->isSupported = 1;
   pRadio->isEnabled = 1;
   pRadio->isHighCapacityInterface = 1;
   pRadio->isTxCapable = 1;
   pRadio->supportedBands = RADIO_HW_SUPPORTED_BAND_23 | RADIO_HW_SUPPORTED_BAND_24 |
        RADIO_HW_SUPPORTED_BAND_25 | RADIO_HW_SUPPORTED_BAND_58;
   pRadio->uCurrentFrequencyKhz = 5800000;
   pRadio->phy_index = 0;
   strncpy(pRadio->szName, szInterface, sizeof(pRadio->szName)-1);
   strcpy(pRadio->szMAC, "001122334455");
   strcpy(pRadio->szDescription, "RubyFPV RF test radio");
   return 1;
}

// The VTX side: the video stream on the air.
static int _test_rf_tx(int iSeconds, const char* szInterface)
{
   if ( ! _test_rf_interface_is_allowed(szInterface) )
      return -1;
   signal(SIGINT, _test_signal_handler);
   signal(SIGTERM, _test_signal_handler);
   printf("\n--- RF TX (the VTX side): the video stream on the air, on %s ---\n", szInterface);

   static t_live_feed_au s_AUs[LIVE_FEED_MAX_AUS];
   u8* pAuData = NULL;
   int iAUs = _test_load_clip_access_units(s_AUs, LIVE_FEED_MAX_AUS, &pAuData);
   if ( iAUs <= 0 )
   {
      printf("  ERROR: the H.264 clip (res/intro.h264) could not be read.\n");
      return -1;
   }

   if ( ! _test_set_radio_interface(0, szInterface) )
   {
      printf("  ERROR: the radio interface could not be prepared.\n");
      free(pAuData);
      return -1;
   }

   // The radio interface is opened for writing with the production code: it
   // creates a pcap handle on the interface and the frames are injected with
   // pcap_inject(). The interface has to be in monitor mode.
   int iFd = radio_open_interface_for_write(0);
   if ( iFd < 0 )
   {
      printf("  ERROR: the radio interface %s could not be opened for writing.\n", szInterface);
      printf("         It needs a monitor mode interface and the CAP_NET_ADMIN / CAP_NET_RAW capabilities.\n");
      free(pAuData);
      return -1;
   }
   printf("  The radio interface %s is open for writing (fd: %d).\n", szInterface, iFd);

   u32 uStreamPacketIndex = 1;
   u16 uFrameIndex = 1;
   u32 uBlockIndex = 1;
   int iAuIndex = 0;
   int iAuSent = 0;
   int iPacketsSent = 0;
   int iFramesSent = 0;
   int iErrors = 0;
   u32 uTimeStart = get_current_timestamp_ms();
   u32 uTimeNextFrame = uTimeStart;

   while ( ! g_bQuit )
   {
      g_TimeNow = get_current_timestamp_ms();
      if ( g_TimeNow - uTimeStart > (u32)(iSeconds * 1000) )
         break;

      t_live_feed_au* pAU = &s_AUs[iAuIndex];
      int iPackets = (pAU->iSize + LIVE_FEED_MAX_VIDEO_DATA - 1) / LIVE_FEED_MAX_VIDEO_DATA;
      if ( iPackets > 255 )
         break;

      int iPos = 0;
      int iPacketInAU = 0;
      while ( iPacketInAU < iPackets )
      {
         // The number of the data packets of this block. The last block of an
         // access unit can have less than 4 packets.
         int iBlockPackets = LIVE_FEED_BLOCK_DATA_PACKETS;
         if ( iPacketInAU + iBlockPackets > iPackets )
            iBlockPackets = iPackets - iPacketInAU;

         for( int i=0; i<iBlockPackets; i++ )
         {
            int iDataSize = pAU->iSize - iPos;
            if ( iDataSize > LIVE_FEED_MAX_VIDEO_DATA )
               iDataSize = LIVE_FEED_MAX_VIDEO_DATA;

            u32 uVideoStatusFlags2 = 0;
            if ( 0 == iPacketInAU )
            {
               uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_START;
               uVideoStatusFlags2 |= pAU->bIsKeyFrame ? VIDEO_STATUS_FLAGS2_IS_NAL_I : VIDEO_STATUS_FLAGS2_IS_NAL_P;
            }
            if ( iPacketInAU == iPackets - 1 )
               uVideoStatusFlags2 |= VIDEO_STATUS_FLAGS2_IS_NAL_END | VIDEO_STATUS_FLAGS2_IS_END_OF_FRAME;

            // The video stream info (the video size) is sent in rotation, the
            // same way the vehicle side sends it.
            u8 uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_NONE;
            u32 uStreamInfo = 0;
            if ( (0 == iPacketInAU) && (0 == (iAuSent % 15)) )
            {
               uStreamInfoFlags = VIDEO_STREAM_INFO_FLAG_SIZE;
               uStreamInfo = (u32)((TEST_VIDEO_WIDTH & 0xFFFF) | ((TEST_VIDEO_HEIGHT & 0xFFFF) << 16));
            }

            // The radio video packet, built the same way the vehicle side
            // builds it.
            u8 uPacket[MAX_PACKET_TOTAL_SIZE];
            int iLen = _build_video_packet_ex(uPacket, sizeof(uPacket),
                  TEST_VID, uStreamPacketIndex++, uFrameIndex, uBlockIndex, (u8)i,
                  (u8)iBlockPackets, 0, (u16)LIVE_FEED_BLOCK_PACKET_SIZE,
                  (u16)((iPackets << 8) | (iPacketInAU + i)), uVideoStatusFlags2,
                  pAuData + pAU->iOffset + iPos, iDataSize,
                  uStreamInfoFlags, uStreamInfo);
            if ( iLen <= 0 )
            {
               iErrors++;
               break;
            }

            // The RF frame: the radiotap header, the 802.11 header and the
            // radio packet, built by the production TX code. The port is the
            // downlink port, the same port the vehicle side uses.
            u8 uFrame[MAX_PACKET_TOTAL_SIZE + 128];
            int iFrameLength = radio_build_new_raw_ieee_packet(0, uFrame, uPacket, iLen,
                  RADIO_PORT_ROUTER_DOWNLINK, 0);
            if ( iFrameLength <= 0 )
            {
               iErrors++;
            }
            else if ( 0 == radio_write_raw_ieee_packet(0, uFrame, iFrameLength, 0) )
            {
               iErrors++;
            }
            else
            {
               iFramesSent++;
               iPacketsSent++;
            }

            iPos += iDataSize;
         }
         uBlockIndex++;
         iPacketInAU += iBlockPackets;
      }

      iAuSent++;
      iAuIndex++;
      uFrameIndex++;
      if ( iAuIndex >= iAUs )
         iAuIndex = 0;

      // The frame rate of the clip: the access units are sent at the rate the
      // video was recorded at.
      uTimeNextFrame += (1000 / LIVE_FEED_FPS);
      u32 uNow = get_current_timestamp_ms();
      if ( uTimeNextFrame > uNow )
         hardware_sleep_ms(uTimeNextFrame - uNow);
   }

   printf("  The video stream was sent on the air for %d seconds: %d access units, %d video packets, %d RF frames, %d errors.\n",
      iSeconds, iAuSent, iPacketsSent, iFramesSent, iErrors);

   radio_close_interface_for_write(0);
   free(pAuData);
   return (0 == iErrors) ? 0 : 1;
}

// The VRX side: the packets received from the air, through the production
// receive path, to the desktop player.
static int _test_rf_rx(int iSeconds, const char* szInterface)
{
   if ( ! _test_rf_interface_is_allowed(szInterface) )
      return -1;
   signal(SIGINT, _test_signal_handler);
   signal(SIGTERM, _test_signal_handler);
   printf("\n--- RF RX (the VRX side): the packets received on %s ---\n", szInterface);

   loadAllModels();

   // The runtime info must match the real vehicle that is transmitting: the
   // video is accepted only for a paired vehicle, and the one that transmits
   // here is the real one (the setup runs this test after the test vehicle
   // setup, which points the runtime info to the synthetic test vehicle).
   g_State.vehiclesRuntimeInfo[0].uVehicleId = getCurrentModel()->uVehicleId;
   g_State.vehiclesRuntimeInfo[0].bIsPairingDone = true;
   g_State.vehiclesRuntimeInfo[0].bReceivedAnyData = true;
   g_State.vehiclesRuntimeInfo[0].bIsDoingRetransmissions = false;

   if ( ! _test_set_radio_interface(0, szInterface) )
   {
      printf("  ERROR: the radio interface could not be prepared.\n");
      return -1;
   }

   // The radio interface is opened for reading with the production code: it
   // creates a pcap handle on the interface, with the filter of the video
   // port. The interface has to be in monitor mode.
   int iFd = radio_open_interface_for_read(0, RADIO_PORT_ROUTER_DOWNLINK);
   if ( iFd < 0 )
   {
      printf("  ERROR: the radio interface %s could not be opened for reading.\n", szInterface);
      printf("         It needs a monitor mode interface and the CAP_NET_ADMIN / CAP_NET_RAW capabilities.\n");
      return -1;
   }
   printf("  The radio interface %s is open for reading (fd: %d).\n", szInterface, iFd);

   // The FIFO: it is created here, the desktop player opens the read end and
   // the station code (the video output) opens the write end. The test does
   // not open the read end: it would compete with the player for the video
   // bytes (a FIFO with two readers splits the stream between them).
   if ( 0 != access(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, F_OK) )
   {
      if ( mkfifo(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, 0666) < 0 )
      {
         printf("  ERROR: failed to create the video streamer FIFO: %s\n", strerror(errno));
         return -1;
      }
   }
   rx_video_output_init();
   _test_enable_streamer_output();
   if ( s_fPipeVideoOutToStreamer < 0 )
   {
      printf("  ERROR: the video output could not open the write end of the FIFO.\n");
      printf("         The desktop video player has to be running (it is the reader).\n");
      return -1;
   }
   printf("  The FIFO write end is open (fd: %d). Waiting for the video packets.\n", s_fPipeVideoOutToStreamer);

   int iRadioPackets = 0;
   int iVideoPackets = 0;
   int iBadPackets = 0;
   int iCRCErrors = 0;
   u32 uTimeStart = get_current_timestamp_ms();

   while ( ! g_bQuit )
   {
      g_TimeNow = get_current_timestamp_ms();
      if ( g_TimeNow - uTimeStart > (u32)(iSeconds * 1000) )
         break;

      int iLength = 0;
      int iDatarate = 0;
      u8* pBuffer = radio_process_wlan_data_in(0, &iLength, &iDatarate, g_TimeNow);
      if ( NULL == pBuffer )
      {
         hardware_sleep_ms(1);
         continue;
      }
      iRadioPackets++;

      // The CRC is checked by the production code, the same way the radio RX
      // thread does it.
      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(0, pBuffer, iLength, &bCRCOk);
      if ( iPacketLength <= 0 )
      {
         iBadPackets++;
         continue;
      }
      if ( ! bCRCOk )
      {
         iCRCErrors++;
         continue;
      }

      t_packet_header* pPH = (t_packet_header*)pBuffer;

      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
         continue;

      // The video packet is handed over to the production video pipeline.
      process_received_video_component_packet(0, pBuffer, iPacketLength);
      iVideoPackets++;
   }

   printf("  The radio interface %s was read for %d seconds.\n", szInterface, iSeconds);
   printf("  The radio packets received: %d, the video packets: %d, the bad packets: %d, the CRC errors: %d.\n",
      iRadioPackets, iVideoPackets, iBadPackets, iCRCErrors);
    printf("  The outputed video packets: %u, the outputed video blocks: %u.\n",
       g_SMControllerRTInfo.uOutputedVideoPackets[g_SMControllerRTInfo.iCurrentIndex],
       g_SMControllerRTInfo.uOutputedVideoBlocks[g_SMControllerRTInfo.iCurrentIndex]);

   radio_close_interface_for_read(0);
   return (iVideoPackets > 0) ? 0 : 1;
}

//---------------------------------------------------------------------------------
// The retransmission loop with the real vehicle code
//
// The vehicle code and the station code can not be linked in the same binary
// (they have their own copies of the shared variables and of the timers), so
// the loop runs in two processes and the capture file is the air:
//
//   1. the vehicle builds the video packets with the production vehicle code
//      and the production radio TX code writes the RF frames to a capture
//      file (test_video_tx_resend --transmit);
//   2. the station reads that capture file with the production radio RX code,
//      one of the video packets is lost on the way, the production station
//      code builds the retransmission request and the production radio TX
//      code writes the request to a second capture file (this mode);
//   3. the vehicle reads the request with the production radio RX code, the
//      production vehicle code answers and the production radio TX code
//      writes the answer to a third capture file (test_video_tx_resend
//      --answer);
//   4. the station reads the answer with the production radio RX code and the
//      video of the frame that had the lost packet has to reach the desktop
//      player (this mode, with the answer file).
//
// The test does not use any radio hardware: the capture files replace the
// air, but every packet that goes through them is built, framed, filtered,
// parsed and CRC checked by the production code.
//---------------------------------------------------------------------------------

// The video packet that is lost on the way to the station. It is a data
// packet of a full video block (a block with no EC packet), so the station
// can not reconstruct it locally: it has to ask the vehicle for it.
#define RTR_LOOP_DROP_INDEX 5

// The id of the controller in the loop: the vehicle refuses the packets that
// come from an unknown controller. It is the same id the vehicle side test
// uses.
#define RTR_LOOP_CONTROLLER_VID 0x87654321

// The video that reached the desktop player FIFO during the test.
static u8 s_uRtrLoopVideo[1024*1024];
static int s_iRtrLoopVideoSize = 0;

// Moves the bytes that are in the video streamer FIFO to the video buffer of
// the test. The FIFO has a limited size, so it has to be read while the
// video is received, the same way the desktop player reads it.
static void _rtr_loop_drain_fifo()
{
   int iGot = _drain_fifo(s_uFifoReceived, sizeof(s_uFifoReceived));
   if ( iGot <= 0 )
      return;
   if ( s_iRtrLoopVideoSize + iGot > (int)sizeof(s_uRtrLoopVideo) )
      iGot = (int)sizeof(s_uRtrLoopVideo) - s_iRtrLoopVideoSize;
   if ( iGot <= 0 )
      return;
   memcpy(s_uRtrLoopVideo + s_iRtrLoopVideoSize, s_uFifoReceived, iGot);
   s_iRtrLoopVideoSize += iGot;
}

// The retransmission loop: the station side.
static int _test_retransmission_loop_with_vehicle(const char* szTxFile, const char* szRequestFile,
      const char* szAnswerFile, int iDropIndex)
{
   printf("\n--- The retransmission loop with the real vehicle code ---\n");

   // The retransmissions are enabled for this test, the same way the models
   // set them: the station has to ask for the packets it did not receive.
   int iCurrentProfile = g_pCurrentModel->video_params.iCurrentVideoProfile;
   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags |=
         VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS | ((u32)DEFAULT_VIDEO_RETRANS_MS5_HP << 8);
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags &= ~((u32)MODEL_RADIOLINKS_FLAGS_DOWNLINK_ONLY);

   _reset_fifo();
   s_iRtrLoopVideoSize = 0;
   packets_queue_init(&s_QueueRadioPacketsHighPrio);

   // The id of this controller: the vehicle checks the source of the packets
   // it receives and it refuses the ones that come from an unknown
   // controller. It is the same id the vehicle test uses.
   g_uControllerId = RTR_LOOP_CONTROLLER_VID;

   // The video output to the desktop player: the production code, the FIFO is
   // the real one.
   rx_video_output_init();
   _test_enable_streamer_output();

   if ( ! _test_setup_test_radio() )
   {
      _check_true("the test radio interface was prepared", false, true);
      return 1;
   }
   radio_init_link_structures();

   ProcessorRxVideo* pProcessor = NULL;

   // The video of the vehicle is received with the production radio RX code.
   if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szTxFile) )
   {
      _check_true("the capture file of the vehicle was opened for reading", false, true);
      return 1;
   }
   _check_true("the capture file of the vehicle was opened for reading", true, true);

   int iCountRead = 0;
   int iCountVideo = 0;
   int iCountDropped = 0;
   int iDroppedBlock = -1;
   int iDroppedPacket = -1;
   int iDroppedFrame = -1;
   int iPayloadLength = 0;
   while( true )
   {
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;
      iCountRead++;

      // The CRC is checked by the production code, the same way the radio RX
      // thread does it.
      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength <= 0) || (! bCRCOk) )
         continue;

      t_packet_header* pPH = (t_packet_header*)pPayload;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
         continue;
      if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
         continue;

      if ( iCountVideo == iDropIndex )
      {
         // The packet is lost: it never reaches the station.
         t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(pPayload + sizeof(t_packet_header));
         iDroppedBlock = (int)pPHVS->uCurrentBlockIndex;
         iDroppedPacket = (int)pPHVS->uCurrentBlockPacketIndex;
         iDroppedFrame = (int)pPHVS->uH264FrameIndex;
         iCountDropped++;
         iCountVideo++;
         continue;
      }
      iCountVideo++;

      // The packet is handed over to the production video pipeline.
      process_received_video_component_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      // The player reads the FIFO while the video is received.
      _rtr_loop_drain_fifo();
   }
   radio_close_interface_for_read(TEST_RADIO_INTERFACE);

   _check("all the RF frames of the vehicle were read by the RX code", iCountRead, iCountVideo);
   _check("exactly one video packet was lost on the way", iCountDropped, 1);
   _check_true("the lost video packet was found", iDroppedBlock >= 0, true);
   if ( iDroppedBlock < 0 )
      return 1;
   printf("  The lost packet: the video block %d, the packet %d of it, the video frame %d.\n",
      iDroppedBlock, iDroppedPacket, iDroppedFrame);

   // The station asks for the missing packet: the request is built by the
   // production code. The video processor of the vehicle is created by the
   // production code when the first video packet arrives.
   pProcessor = ProcessorRxVideo::getVideoProcessorForVehicleId(TEST_VID, 0);
   _check_true("the video processor of the test vehicle exists", NULL != pProcessor, true);
   if ( NULL == pProcessor )
      return 1;

   int iRequested = pProcessor->periodicLoopProcessor(g_TimeNow, true);
   _check("the station requested the missing video packet", iRequested, 1);

   int iRequestLength = 0;
   u8* pRequest = packets_queue_peek_packet(&s_QueueRadioPacketsHighPrio, 0, &iRequestLength);
   _check_true("the retransmission request was built", NULL != pRequest, true);
   if ( NULL == pRequest )
      return 1;

   u32 uRetrId = 0;
   memcpy(&uRetrId, pRequest + sizeof(t_packet_header), sizeof(u32));
   _check_true("the retransmission request has a retransmission id", uRetrId != 0, true);

   // The request has to ask for the lost packet.
   u8 uCount = pRequest[sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8)];
   _check_true("the retransmission request asks for at least one packet", uCount > 0, true);
   bool bFoundLostPacket = false;
   u8* pList = pRequest + sizeof(t_packet_header) + sizeof(u32) + 3*sizeof(u8);
   for( int i=0; i<(int)uCount; i++ )
   {
      u32 uBlockId = 0;
      u8 uPacketIndex = 0;
      memcpy(&uBlockId, pList, sizeof(u32));
      pList += sizeof(u32);
      uPacketIndex = *pList;
      pList += sizeof(u8);
      if ( (uBlockId == (u32)iDroppedBlock) && (uPacketIndex == (u8)iDroppedPacket) )
         bFoundLostPacket = true;
   }
   _check_true("the retransmission request asks for the lost packet", bFoundLostPacket, true);

   // The request goes on the air with the production radio TX code. The
   // station transmits on the uplink: the vehicle receives the requests on it.
   static u8 s_uRequestFrame[MAX_PACKET_TOTAL_SIZE + 64];
   int iFrameLength = radio_build_new_raw_ieee_packet(TEST_RADIO_INTERFACE, s_uRequestFrame,
         pRequest, iRequestLength, RADIO_PORT_ROUTER_UPLINK, 0);
   _check_true("the retransmission request was put on the air", iFrameLength > 0, true);
   if ( iFrameLength > 0 )
   {
      // The radio packet is the last part of the frame: the radiotap header
      // and the 802.11 header are in front of it.
      const u8* pOnAir = s_uRequestFrame + (iFrameLength - iRequestLength);
      bool bSameBytes = (0 == memcmp(pOnAir + 4, pRequest + 4, 10)) &&
            (0 == memcmp(pOnAir + 16, pRequest + 16, iRequestLength - 16));
      _check_true("the bytes on the air are the bytes of the request", bSameBytes, true);
      _check_true("the packet on the air has a good CRC",
            radio_packet_check_crc((u8*)pOnAir, iRequestLength) != 0, true);

      int iFrameSizes[1];
      iFrameSizes[0] = iFrameLength;
      _check_true("the request capture file was written",
            _write_capture_file(szRequestFile, s_uRequestFrame, iFrameSizes, 1) != 0, true);
   }

   // The answer of the vehicle: the last step of the loop.
   if ( (NULL == szAnswerFile) || (0 == strcmp(szAnswerFile, "-")) )
   {
      printf("  The request is in %s. The answer of the vehicle is not checked in this run.\n", szRequestFile);
      return 0;
   }

   if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szAnswerFile) )
   {
      _check_true("the answer capture file was opened for reading", false, true);
      return 1;
   }
   _check_true("the answer capture file was opened for reading", true, true);

   int iCountAnswer = 0;
   int iCountAnswerRetr = 0;
   while( true )
   {
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;

      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength <= 0) || (! bCRCOk) )
         continue;

      t_packet_header* pPH = (t_packet_header*)pPayload;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
         continue;
      if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
         continue;
      iCountAnswer++;
      if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
         iCountAnswerRetr++;

      // The answer is handed over to the production video pipeline, the same
      // way the radio RX thread does it.
      process_received_video_component_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      _rtr_loop_drain_fifo();
   }
   radio_close_interface_for_read(TEST_RADIO_INTERFACE);
   _rtr_loop_drain_fifo();

   _check_true("the answer of the vehicle contains video packets", iCountAnswer > 0, true);
   _check("all the packets of the answer are marked as retransmissions", iCountAnswerRetr, iCountAnswer);

   // The video of the frame that had the lost packet has to reach the desktop
   // player. The data is the access unit of the clip, byte for byte.
   static t_live_feed_au s_AUs[LIVE_FEED_MAX_AUS];
   u8* pAuData = NULL;
   int iAUs = _test_load_clip_access_units(s_AUs, LIVE_FEED_MAX_AUS, &pAuData);
   _check_true("the access units of the clip were loaded", iAUs > 0, true);
   if ( iAUs <= 0 )
      return 1;

   // The video frame index of the vehicle starts at 1 and it is the index of
   // the access unit of the clip.
   int iAuIndex = iDroppedFrame - 1;
   _check_true("the lost packet was in an access unit of the clip",
         (iAuIndex >= 0) && (iAuIndex < iAUs), true);
   if ( (iAuIndex < 0) || (iAuIndex >= iAUs) )
   {
      free(pAuData);
      return 1;
   }

   // The video that reached the player starts with the first access unit of
   // the clip and it is a byte for byte copy of the access units.
   int iExpectedSize = 0;
   for( int i=0; i<iAUs; i++ )
      iExpectedSize += s_AUs[i].iSize;
   int iVideoSize = s_iRtrLoopVideoSize;
   if ( iVideoSize > iExpectedSize )
      iVideoSize = iExpectedSize;

   bool bVideoOk = (s_iRtrLoopVideoSize > 0);
   if ( bVideoOk )
      bVideoOk = (0 == memcmp(s_uRtrLoopVideo, pAuData + s_AUs[0].iOffset, iVideoSize));
   _check_true("the video that reached the player is the video of the clip", bVideoOk, true);
   printf("  The video that reached the player: %d bytes (the clip has %d bytes of access units).\n",
      s_iRtrLoopVideoSize, iExpectedSize);

   // The access unit with the lost packet has to be in the video: the frame
   // was completed only after the retransmission arrived.
   int iOffsetOfLostAU = 0;
   for( int i=0; i<iAuIndex; i++ )
      iOffsetOfLostAU += s_AUs[i].iSize;
   int iLostAUSize = s_AUs[iAuIndex].iSize;
   bool bLostAUDelivered = (s_iRtrLoopVideoSize >= iOffsetOfLostAU + iLostAUSize);
   _check_true("the access unit with the lost packet reached the player", bLostAUDelivered, true);
   if ( bLostAUDelivered )
   {
      bool bLostAUOk = (0 == memcmp(s_uRtrLoopVideo + iOffsetOfLostAU, pAuData + s_AUs[iAuIndex].iOffset, iLostAUSize));
      _check_true("the access unit with the lost packet is byte for byte correct", bLostAUOk, true);
   }

   _check_true("the retransmitted packet was counted as retransmitted",
         g_SMControllerRTInfo.uOutputedVideoPacketsRetransmitted[g_SMControllerRTInfo.iCurrentIndex] > 0, true);
   _check_true("the retransmitted packet was not discarded",
         0 == g_SMControllerRTInfo.uOutputedVideoPacketsRetransmittedDiscarded[g_SMControllerRTInfo.iCurrentIndex], true);

   free(pAuData);
   return 0;
}

//---------------------------------------------------------------------------------
// The sustained retransmission loop, with the real desktop player.
//
// It is the same loop as the one above, but it is repeated many times and the
// reader of the FIFO is the desktop player, not the test: the video that the
// production code writes is decoded and displayed by the real player.
//
// The vehicle is a separate process (the vehicle code and the station code
// have their own copy of the shared variables, they can not be linked in the
// same binary), so the station runs the vehicle test binary to answer every
// request: the answer is produced by the production vehicle code, exactly as
// in the loop above.
//---------------------------------------------------------------------------------

// The watchdog of the live loop. The production code writes the video to the
// FIFO of the player with a blocking write, so a player that does not read
// the video would block the test forever.
static void _rtr_loop_watchdog(int iSignal)
{
   const char* szMsg = "\n  ERROR: the watchdog stopped the test: the desktop player did not read the video FIFO.\n";
   write(STDERR_FILENO, szMsg, strlen(szMsg));
   _exit(3);
}

// Runs the vehicle test binary, so it answers the request of the station with
// the production vehicle code. It returns 1 if the vehicle answered.
static int _rtr_loop_run_vehicle(const char* szVehicleBinary, const char* szRequestFile, const char* szAnswerFile)
{
   pid_t pid = fork();
   if ( pid < 0 )
   {
      printf("  ERROR: the vehicle process could not be started: %s\n", strerror(errno));
      return 0;
   }

   if ( 0 == pid )
   {
      // The child: the vehicle builds its video and answers the request. Its
      // output goes to a log file, so that the output of the test stays
      // readable.
      char szLogFile[512];
      snprintf(szLogFile, sizeof(szLogFile), "%s.vehicle.log", szAnswerFile);
      int fd = open(szLogFile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      if ( fd >= 0 )
      {
         dup2(fd, STDOUT_FILENO);
         dup2(fd, STDERR_FILENO);
         close(fd);
      }
      char* argv[5];
      argv[0] = (char*)szVehicleBinary;
      argv[1] = (char*)"--answer";
      argv[2] = (char*)szRequestFile;
      argv[3] = (char*)szAnswerFile;
      argv[4] = NULL;
      execv(szVehicleBinary, argv);
      _exit(127);
   }

   int iStatus = 0;
   if ( waitpid(pid, &iStatus, 0) < 0 )
   {
      printf("  ERROR: the vehicle process could not be waited for: %s\n", strerror(errno));
      return 0;
   }
   if ( ! WIFEXITED(iStatus) )
   {
      printf("  ERROR: the vehicle process did not end normally.\n");
      return 0;
   }
   if ( 0 != WEXITSTATUS(iStatus) )
   {
      printf("  ERROR: the vehicle process failed (status: %d).\n", WEXITSTATUS(iStatus));
      return 0;
   }
   return 1;
}

// The sustained retransmission loop, with the real desktop player.
static int _test_retransmission_loop_live(const char* szTxFile, const char* szRequestFile,
      const char* szAnswerFile, const char* szVehicleBinary, int iIterations)
{
   printf("\n--- The sustained retransmission loop, with the real desktop player ---\n");
   printf("  The desktop video player has to be running: it is the reader of the FIFO.\n");

   if ( iIterations <= 0 )
      iIterations = 1;

   // The retransmissions are enabled for this test, the same way the models
   // set them: the station has to ask for the packets it did not receive.
   int iCurrentProfile = g_pCurrentModel->video_params.iCurrentVideoProfile;
   g_pCurrentModel->video_link_profiles[iCurrentProfile].uProfileEncodingFlags |=
         VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS | ((u32)DEFAULT_VIDEO_RETRANS_MS5_HP << 8);
   g_pCurrentModel->radioLinksParams.uGlobalRadioLinksFlags &= ~((u32)MODEL_RADIOLINKS_FLAGS_DOWNLINK_ONLY);

   // The id of this controller: the vehicle refuses the packets that come from
   // an unknown controller.
   g_uControllerId = RTR_LOOP_CONTROLLER_VID;

   // The video output to the desktop player: the production code opens the
   // write end of the FIFO. The open fails if there is no reader (the player).
   // The test does NOT open the read end of the FIFO: it would compete with
   // the player for the video bytes.
   rx_video_output_init();
   _test_enable_streamer_output();
   if ( s_fPipeVideoOutToStreamer < 0 )
   {
      printf("  ERROR: the video output could not open the write end of the FIFO.\n");
      printf("         The desktop video player has to be running (it is the reader).\n");
      return 1;
   }
   printf("  The FIFO write end is open (fd: %d).\n", s_fPipeVideoOutToStreamer);

   if ( ! _test_setup_test_radio() )
   {
      _check_true("the test radio interface was prepared", false, true);
      return 1;
   }
   radio_init_link_structures();

   // The watchdog: the write to the FIFO of the player is a blocking one.
   signal(SIGALRM, _rtr_loop_watchdog);

   unsigned long long uBytesBefore = rx_video_output_get_bytes_to_local_video_player();
   int iIterationsDone = 0;
   int iTotalLost = 0;
   int iTotalRequested = 0;
   int iTotalAnswered = 0;
   int iTotalFailed = 0;

   for( int iIteration = 0; iIteration < iIterations; iIteration++ )
   {
      g_TimeNow = get_current_timestamp_ms();

      // The packet that is lost in this iteration: a data packet of a full
      // block (a block with 4 data packets and no EC packet), so the station
      // can not reconstruct it locally and it has to ask the vehicle for it.
      // The position of the lost packet in the block changes from one
      // iteration to the other, so every position of the block is tested.
      u8 uDropPacketInBlock = (u8)(1 + (iIteration % 3));

      // The state of the video reception is reset, so the same capture file
      // can be received again. It is the production reset of the processor.
      ProcessorRxVideo* pProcessor = ProcessorRxVideo::getVideoProcessorForVehicleId(TEST_VID, 0);
      if ( NULL != pProcessor )
         pProcessor->fullResetState("the sustained retransmission loop");

      // The queue of the packets to be sent is emptied: the request of this
      // iteration is the only one in it.
      packets_queue_init(&s_QueueRadioPacketsHighPrio);

      // The video of the vehicle is received with the production radio RX
      // code and one packet is lost on the way.
      if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szTxFile) )
      {
         _check_true("the capture file of the vehicle was opened for reading", false, true);
         return 1;
      }

      int iCountVideo = 0;
      int iCountLost = 0;
      int iLostBlock = -1;
      int iLostPacket = -1;
      int iPayloadLength = 0;
      while( true )
      {
         u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
         if ( NULL == pPayload )
            break;

         int bCRCOk = 0;
         int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
         if ( (iPacketLength <= 0) || (! bCRCOk) )
            continue;

         t_packet_header* pPH = (t_packet_header*)pPayload;
         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
            continue;
         if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
            continue;

         iCountVideo++;
         t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(pPayload + sizeof(t_packet_header));

         // The packet is lost when it is a data packet of a full block (a
         // block with 4 data packets and no EC packet) and it is the packet
         // of the block that is dropped in this iteration.
         if ( (pPHVS->uCurrentBlockDataPackets == 4) && (pPHVS->uCurrentBlockECPackets == 0) &&
              (pPHVS->uCurrentBlockPacketIndex == uDropPacketInBlock) )
         {
            iLostBlock = (int)pPHVS->uCurrentBlockIndex;
            iLostPacket = (int)pPHVS->uCurrentBlockPacketIndex;
            iCountLost++;
            continue;
         }

         // The packet is handed over to the production video pipeline.
         process_received_video_component_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      }
      radio_close_interface_for_read(TEST_RADIO_INTERFACE);

      if ( (iCountLost != 1) || (iLostBlock < 0) )
      {
         printf("  ERROR: the iteration %d lost %d packets (the block %d).\n", iIteration, iCountLost, iLostBlock);
         iTotalFailed++;
         continue;
      }
      iTotalLost += iCountLost;

      // The station asks for the missing packet: the request is built by the
      // production code.
      pProcessor = ProcessorRxVideo::getVideoProcessorForVehicleId(TEST_VID, 0);
      if ( NULL == pProcessor )
      {
         printf("  ERROR: the video processor of the test vehicle does not exist.\n");
         iTotalFailed++;
         continue;
      }

      int iRequested = pProcessor->periodicLoopProcessor(g_TimeNow, true);
      if ( iRequested < 1 )
      {
         printf("  ERROR: the iteration %d: the station did not ask for the missing packet.\n", iIteration);
         iTotalFailed++;
         continue;
      }
      iTotalRequested += iRequested;

      int iRequestLength = 0;
      u8* pRequest = packets_queue_peek_packet(&s_QueueRadioPacketsHighPrio, 0, &iRequestLength);
      if ( NULL == pRequest )
      {
         printf("  ERROR: the iteration %d: the retransmission request was not built.\n", iIteration);
         iTotalFailed++;
         continue;
      }

      // The request has to ask for the lost packet.
      u32 uRetrId = 0;
      memcpy(&uRetrId, pRequest + sizeof(t_packet_header), sizeof(u32));
      u8 uCount = pRequest[sizeof(t_packet_header) + sizeof(u32) + 2*sizeof(u8)];
      bool bFoundLostPacket = (0 != uRetrId);
      u8* pList = pRequest + sizeof(t_packet_header) + sizeof(u32) + 3*sizeof(u8);
      for( int i=0; i<(int)uCount; i++ )
      {
         u32 uBlockId = 0;
         u8 uPacketIndex = 0;
         memcpy(&uBlockId, pList, sizeof(u32));
         pList += sizeof(u32);
         uPacketIndex = *pList;
         pList += sizeof(u8);
         if ( (uBlockId == (u32)iLostBlock) && (uPacketIndex == (u8)iLostPacket) )
            bFoundLostPacket = true;
      }
      if ( ! bFoundLostPacket )
      {
         printf("  ERROR: the iteration %d: the request does not ask for the lost packet.\n", iIteration);
         iTotalFailed++;
         continue;
      }

      // The request goes on the air with the production radio TX code. The
      // station transmits on the uplink: the vehicle receives the requests on it.
      static u8 s_uRequestFrame[MAX_PACKET_TOTAL_SIZE + 64];
      int iFrameLength = radio_build_new_raw_ieee_packet(TEST_RADIO_INTERFACE, s_uRequestFrame,
            pRequest, iRequestLength, RADIO_PORT_ROUTER_UPLINK, 0);
      if ( (iFrameLength <= 0) ||
           (0 == _write_capture_file(szRequestFile, s_uRequestFrame, &iFrameLength, 1)) )
      {
         printf("  ERROR: the iteration %d: the request could not be put on the air.\n", iIteration);
         iTotalFailed++;
         continue;
      }

      // The vehicle answers the request, with the production vehicle code.
      unlink(szAnswerFile);
      if ( ! _rtr_loop_run_vehicle(szVehicleBinary, szRequestFile, szAnswerFile) )
      {
         printf("  ERROR: the iteration %d: the vehicle did not answer the request.\n", iIteration);
         iTotalFailed++;
         continue;
      }

      // The answer of the vehicle is received with the production radio RX
      // code and it is handed over to the production video pipeline.
      if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szAnswerFile) )
      {
         printf("  ERROR: the iteration %d: the answer could not be opened for reading.\n", iIteration);
         iTotalFailed++;
         continue;
      }

      int iCountAnswer = 0;
      int iCountAnswerRetr = 0;
      while( true )
      {
         u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
         if ( NULL == pPayload )
            break;

         int bCRCOk = 0;
         int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
         if ( (iPacketLength <= 0) || (! bCRCOk) )
            continue;

         t_packet_header* pPH = (t_packet_header*)pPayload;
         if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
            continue;
         if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
            continue;
         iCountAnswer++;
         if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
            iCountAnswerRetr++;

         process_received_video_component_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      }
      radio_close_interface_for_read(TEST_RADIO_INTERFACE);

      if ( (iCountAnswer < 1) || (iCountAnswerRetr != iCountAnswer) )
      {
         printf("  ERROR: the iteration %d: the answer has %d packets, %d of them are retransmissions.\n",
               iIteration, iCountAnswer, iCountAnswerRetr);
         iTotalFailed++;
         continue;
      }
      iTotalAnswered += iCountAnswer;
      iIterationsDone++;

      printf("  The iteration %d: %d video packets received, the packet %d of the block %d was lost, the station asked for it, the vehicle answered it.\n",
            iIteration, iCountVideo, iLostPacket, iLostBlock);
   }

   unsigned long long uBytesAfter = rx_video_output_get_bytes_to_local_video_player();
   unsigned long long uBytesToPlayer = uBytesAfter - uBytesBefore;

   printf("\n  The sustained retransmission loop: %d iteration(s), %d packet(s) lost, %d request(s), %d answer(s), %d failure(s).\n",
         iIterationsDone, iTotalLost, iTotalRequested, iTotalAnswered, iTotalFailed);
   printf("  The video that reached the desktop player: %llu bytes.\n", (unsigned long long)uBytesToPlayer);

   _check("all the iterations of the sustained loop succeeded", iTotalFailed, 0);
   _check("every iteration lost a video packet", iTotalLost, iIterationsDone);
   _check("every iteration got an answer from the vehicle", iTotalAnswered, iIterationsDone);
   _check_true("the video reached the desktop player", uBytesToPlayer > 0, true);

   return (0 == iTotalFailed) ? 0 : 1;
}

int main(int argc, char* argv[])
{
   printf("================================================================\n");
   printf(" RubyFPV video RX pipeline test\n");
   printf("================================================================\n");

   // The live feed mode: the video stream is sent through the production RX
   // pipeline, in real time, to the desktop video player. It is not a test
   // case, it is the live demonstration of the video path.
   if ( (argc > 1) && (0 == strcmp(argv[1], "--live-feed")) )
   {
      int iSeconds = 30;
      if ( argc > 2 )
         iSeconds = atoi(argv[2]);
      _test_setup_environment();
      int iRes = _test_live_feed(iSeconds);
      return (0 == iRes) ? 0 : 1;
   }

   // The two-radio RF test: the VTX side, the video stream on the air.
   if ( (argc > 1) && (0 == strcmp(argv[1], "--rf-tx")) )
   {
      int iSeconds = 30;
      if ( argc > 2 )
         iSeconds = atoi(argv[2]);
      const char* szInterface = NULL;
      if ( argc > 3 )
         szInterface = argv[3];
      _test_setup_environment();
      int iRes = _test_rf_tx(iSeconds, szInterface);
      return (0 == iRes) ? 0 : 1;
   }

   // The two-radio RF test: the VRX side, the packets from the air to the
   // desktop player.
   if ( (argc > 1) && (0 == strcmp(argv[1], "--rf-rx")) )
   {
      int iSeconds = 30;
      if ( argc > 2 )
         iSeconds = atoi(argv[2]);
      const char* szInterface = NULL;
      if ( argc > 3 )
         szInterface = argv[3];
      _test_setup_environment();
      int iRes = _test_rf_rx(iSeconds, szInterface);
      // The exit status is a contract with the test script:
      //    0   = the video packets were received and they reached the video pipeline
      //    1   = the interface was read, but no video packet arrived
      //    255 = the interface could not be opened at all
      if ( iRes < 0 )
         return 255;
      return (0 == iRes) ? 0 : 1;
   }

   // The retransmission loop with the real vehicle code: the station reads
   // the video of the vehicle, one packet is lost on the way, the station
   // builds the retransmission request and puts it in a capture file. When
   // the answer of the vehicle is given, the answer is read too and the video
   // has to reach the desktop player.
   if ( (argc > 5) && (0 == strcmp(argv[1], "--rtr-loop")) )
   {
      _test_setup_environment();
      _test_open_fifo();
      int iRes = _test_retransmission_loop_with_vehicle(argv[2], argv[3], argv[4], atoi(argv[5]));
      if ( s_iTestFifoReadFd >= 0 )
         close(s_iTestFifoReadFd);
      printf("\n================================================================\n");
      printf(" Tests: %d, Failures: %d\n", s_iTests, s_iFailures);
      printf("================================================================\n");
      return (0 == iRes) ? ((0 == s_iFailures) ? 0 : 1) : 1;
   }

   // The sustained retransmission loop with the real desktop player: the
   // player reads the FIFO, the station loses a video packet in every
   // iteration, it asks the vehicle for it and the vehicle (a separate
   // process, it can not be linked in this binary) answers it with the
   // production vehicle code.
   if ( (argc > 6) && (0 == strcmp(argv[1], "--rtr-loop-live")) )
   {
      _test_setup_environment();
      int iRes = _test_retransmission_loop_live(argv[2], argv[3], argv[4], argv[5], atoi(argv[6]));
      printf("\n================================================================\n");
      printf(" Tests: %d, Failures: %d\n", s_iTests, s_iFailures);
      printf("================================================================\n");
      return (0 == iRes) ? ((0 == s_iFailures) ? 0 : 1) : 1;
   }

   _test_setup_environment();
   _test_open_fifo();

   _test_packet_builder();
   if ( _test_load_video_payload() > 0 )
   {
      _test_receive_complete_frame();
      _test_receive_order();
      _test_incomplete_block();
      _test_bad_crc();
      _test_malformed_scheme_changed();
      _test_unknown_vehicle();
      _test_ec_reconstruction();
      _test_recording();

      // The radio path: the packets go through the real RF framing, the real
      // pcap filter and the real RX parsing.
      _test_radio_path();
      _test_sustained_radio_path();

      // The retransmission: the station asks the vehicle to send again the
      // video packets it did not receive.
      _test_retransmission_request();
      _test_retransmission_loop();

      // The forwarding to a second player, over UDP.
      _test_forward_to_local_player_udp();

      // The radio path with the radiotap header that a real monitor interface
      // delivers.
      _test_radio_path_with_monitor_radiotap();
   }
   else
   {
      printf("\nThe H.264 test payload could not be loaded, the RX tests were skipped.\n");
      s_iFailures++;
   }

   if ( s_iTestFifoReadFd >= 0 )
      close(s_iTestFifoReadFd);

   printf("\n================================================================\n");
   printf(" Tests: %d, Failures: %d\n", s_iTests, s_iFailures);
   printf("================================================================\n");

   return (0 == s_iFailures) ? 0 : 1;
}
