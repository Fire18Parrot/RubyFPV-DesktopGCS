/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of any source or binary form, with or without
          modification, are permitted provided that the following conditions
          are met:
        * Redistributions of source code (partially or complete) must retain
          the above copyright notice, this list of conditions and the following
          disclaimer in the documentation and/or other materials provided with
          the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
          the above copyright notice, this list of conditions and the following
          disclaimer in the documentation and/or other materials provided with
          the distribution.
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

// The vehicle side video TX regression test.
//
// It runs the REAL vehicle code that builds the video packets and puts them on
// the air:
//
//   VideoTxPacketsBuffer::appendDataToCurrentFrame()   (code/r_vehicle/video_tx_buffers.cpp)
//     -> VideoTxPacketsBuffer::_addNewVideoPacket()
//       -> VideoTxPacketsBuffer::sendAvailablePackets()
//         -> VideoTxPacketsBuffer::_sendPacket()
//           -> send_packet_to_radio_interfaces()        (code/r_vehicle/packets_utils.cpp)
//             -> _send_packet_to_wifi_radio_interface()
//               -> radio_build_new_raw_ieee_packet()     (code/radio/radiolink.c)
//               -> radio_write_raw_ieee_packet()
//
// and the REAL vehicle code that answers a retransmission request:
//
//   process_received_single_radio_packet()              (code/r_vehicle/process_radio_in_packets.cpp)
//     -> process_data_tx_video_command()                 (code/r_vehicle/processor_tx_video.cpp)
//       -> VideoTxPacketsBuffer::resendVideoPacket()
//         -> VideoTxPacketsBuffer::_sendPacket()
//
// The RF frames are written to a pcap capture file that has the same link type
// as a monitor mode capture of a real link (DLT_IEEE802_11_RADIO), so they can
// be read back with the production RX code (radio_open_interface_for_read_from_file()).
// The capture file stands for the air: the bytes in it are the bytes that
// would have gone on the air, with the same radiotap header, the same 802.11
// header and the same CRC.
//
// The test does not need any radio hardware, no monitor mode, no root
// privileges and no network access. The video is the in-tree known-good H.264
// clip (res/intro.h264), fed to the camera input of the vehicle the same way
// the camera capture feeds it.
//
// Modes:
//   --transmit <capture file>              the vehicle builds the video and
//                                           sends it (the normal transmission)
//   --answer <request file> <capture file> the vehicle receives the
//                                           retransmission request of the
//                                           station and answers it
//
// The two modes are the two halves of the retransmission loop and they are
// run by the same script that runs the station side
// (code/r_tests/video_retransmission_loop_test.sh).

#include "../base/base.h"
#include "../base/config.h"
#include "../base/flags_video.h"
#include "../base/hardware_radio.h"
#include "../base/models.h"
#include "../base/models_list.h"
#include "../base/parser_h264.h"
#include "../base/shared_mem.h"
#include "../common/string_utils.h"
#include "../radio/radiolink.h"
#include "../radio/radiopackets2.h"
#include "../r_vehicle/packets_utils.h"
#include "../r_vehicle/processor_tx_video.h"
#include "../r_vehicle/process_radio_in_packets.h"
#include "../r_vehicle/shared_vars.h"
#include "../r_vehicle/video_tx_buffers.h"

#include <pcap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_VID 0x12345678
#define TEST_CONTROLLER_VID 0x87654321

// The video profile of the test: 4 data packets in a block, no EC packets
// (a lost packet can not be reconstructed locally, so the station has to ask
// for it) and 1200 bytes in a packet.
#define TEST_BLOCK_DATA_PACKETS 4
#define TEST_BLOCK_PACKET_SIZE 1200

#define TEST_RADIO_INTERFACE 0
#define TEST_VIDEO_WIDTH 1280
#define TEST_VIDEO_HEIGHT 720

// The number of the access units of the clip that are sent to the vehicle.
#define TEST_MAX_ACCESS_UNITS 8

//---------------------------------------------------------------------------------
// Test-only stubs
//
// A few functions of the vehicle live in ruby_rt_vehicle.cpp, next to its
// main(). Linking that file into a test is not possible (two main()
// functions), and the functions themselves are about the lifetime of the
// vehicle process (the long operation signals, the radio reconfiguration
// flags), not about the video path.
//
// These are the smallest possible test-only seams: the code under test does
// not depend on their result for the video path.
//---------------------------------------------------------------------------------

void signal_start_long_op()
{
   log_line("[TestVideoTx] signal_start_long_op");
}

void signal_end_long_op()
{
   log_line("[TestVideoTx] signal_end_long_op");
}

void checkDeveloperFlagsChanges(u32 uOldDeveloperFlags, u32 uNewDeveloperFlags)
{
   log_line("[TestVideoTx] checkDeveloperFlagsChanges: %u -> %u", uOldDeveloperFlags, uNewDeveloperFlags);
}

void flag_send_radio_config_to_controller()
{
   log_line("[TestVideoTx] flag_send_radio_config_to_controller");
}

void flag_reinit_sik_interface(int iInterfaceIndex)
{
   log_line("[TestVideoTx] flag_reinit_sik_interface: %d", iInterfaceIndex);
}

//---------------------------------------------------------------------------------
// The checks
//---------------------------------------------------------------------------------

static int s_iTests = 0;
static int s_iFailures = 0;

static void _check(const char* szName, long long iValue, long long iExpected)
{
   s_iTests++;
   if ( iValue == iExpected )
   {
      printf("  [ OK ] %s: %lld\n", szName, iValue);
      return;
   }
   printf("  [FAIL] %s: %lld (expected %lld)\n", szName, iValue, iExpected);
   s_iFailures++;
}

static void _check_true(const char* szName, bool bValue, bool bExpected)
{
   s_iTests++;
   if ( bValue == bExpected )
   {
      printf("  [ OK ] %s: %s\n", szName, bValue?"yes":"no");
      return;
   }
   printf("  [FAIL] %s: %s (expected %s)\n", szName, bValue?"yes":"no", bExpected?"yes":"no");
   s_iFailures++;
}

//---------------------------------------------------------------------------------
// The test environment
//---------------------------------------------------------------------------------

static shared_mem_process_stats s_TestProcessStats;

// Reads a file into a buffer. It returns the number of bytes read.
static int _read_file(const char* szFileName, u8* pBuffer, int iMaxSize)
{
   FILE* fd = fopen(szFileName, "rb");
   if ( NULL == fd )
      return 0;
   int iRead = (int)fread(pBuffer, 1, iMaxSize, fd);
   fclose(fd);
   return iRead;
}

// Splits an Annex-B elementary stream into NAL units. It returns the number of
// NAL units found and fills the array with the offsets and the sizes of each
// NAL unit (the start code is not included).
static int _split_annexb_nals(const u8* pData, int iSize, int* pOffsets, int* pSizes, int iMaxNals)
{
   int iCount = 0;
   int iPos = 0;
   while ( iPos + 3 < iSize )
   {
      if ( pData[iPos] == 0 && pData[iPos+1] == 0 && pData[iPos+2] == 1 )
      {
         int iStart = iPos + 3;
         int iNext = iStart;
         while ( iNext + 3 < iSize )
         {
            if ( pData[iNext] == 0 && pData[iNext+1] == 0 && pData[iNext+2] == 1 )
               break;
            iNext++;
         }
         int iEnd = iNext;
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

// Prepares the vehicle side environment: the model, the radio link, the radio
// interface and the video TX buffers.
//
// The radio interface is a real entry of the radio hardware list (the same
// entry that the vehicle code uses), it is opened for writing to a capture
// file, so the production TX code has a radio to write to.
static radio_hw_info_t s_oSavedRadioEntry;
static int s_bRadioEntrySaved = 0;

static void _setup_environment()
{
   log_init_local_only("TestVideoTx");
   log_enable_stdout();

   g_TimeStart = get_current_timestamp_ms();
   g_TimeNow = g_TimeStart;

   memset(&s_TestProcessStats, 0, sizeof(shared_mem_process_stats));
   g_pProcessStats = &s_TestProcessStats;

   // The vehicle is not paired with a controller yet: the test sets that up
   // when it needs the vehicle to answer a retransmission request.
   g_bVideoPaused = false;
   g_bReceivedPairingRequest = true;
   g_uControllerId = TEST_CONTROLLER_VID;

   // The radio link structures (the radio link packet indexes, the radio
   // flags and the datarates) are needed by the TX code.
   radio_init_link_structures();

   // The models list is loaded first (it is needed by getCurrentModel()).
   // loadAllModels() only reads the configuration files, it does not write.
   loadAllModels();

   Model* pModel = getCurrentModel();
   pModel->resetToDefaults(false);
   pModel->uVehicleId = TEST_VID;
   pModel->sw_version = (11 * 256) + 6;
   pModel->video_params.iVideoWidth = TEST_VIDEO_WIDTH;
   pModel->video_params.iVideoHeight = TEST_VIDEO_HEIGHT;
   pModel->video_params.iVideoFPS = 25;
   pModel->video_params.iCurrentVideoProfile = VIDEO_PROFILE_HIGH_QUALITY;

   // The video profile of the test: 4 data packets in a block, no EC packets
   // and 1200 bytes in a packet.
   type_video_link_profile* pProfile = &(pModel->video_link_profiles[VIDEO_PROFILE_HIGH_QUALITY]);
   pProfile->iBlockDataPackets = TEST_BLOCK_DATA_PACKETS;
   pProfile->iECPercentage = 0;
   pProfile->iBlockECs = 0;
   pProfile->video_data_length = TEST_BLOCK_PACKET_SIZE;

   // The vehicle has a camera: the radio RX code checks it before it hands
   // the video requests over to the video TX code.
   pModel->iCameraCount = 1;
   pModel->iCurrentCamera = 0;
   pModel->camera_params[0].iCameraType = CAMERA_TYPE_CSI;
   pModel->camera_params[0].iForcedCameraType = CAMERA_TYPE_CSI;

   // One radio link and one radio interface on it. The link can be used for
   // video and for data, the interface can transmit.
   pModel->radioLinksParams.links_count = 1;
   pModel->radioLinksParams.link_capabilities_flags[0] = RADIO_HW_CAPABILITY_FLAG_CAN_RX |
         RADIO_HW_CAPABILITY_FLAG_CAN_TX | RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO |
         RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA | RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY;
   pModel->radioLinksParams.link_frequency_khz[0] = 5800000;
   pModel->radioLinksParams.link_radio_flags_tx[0] = RADIO_FLAGS_FRAME_TYPE_DATA |
         RADIO_FLAGS_USE_LEGACY_DATARATES | RADIO_FLAGS_USE_MCS_DATARATES | RADIO_FLAG_HT40;
   pModel->radioLinksParams.link_radio_flags_rx[0] = RADIO_FLAGS_FRAME_TYPE_DATA |
         RADIO_FLAGS_USE_LEGACY_DATARATES | RADIO_FLAGS_USE_MCS_DATARATES | RADIO_FLAG_HT40;
   pModel->radioLinksParams.downlink_datarate_video_bps[0] = 20000000;
   pModel->radioLinksParams.uplink_datarate_video_bps[0] = 20000000;
   pModel->radioLinksParams.downlink_datarate_data_bps[0] = 10000000;
   pModel->radioLinksParams.uplink_datarate_data_bps[0] = 10000000;

   pModel->radioInterfacesParams.interfaces_count = 1;
   pModel->radioInterfacesParams.interface_link_id[0] = 0;
   pModel->radioInterfacesParams.interface_capabilities_flags[0] = RADIO_HW_CAPABILITY_FLAG_CAN_RX |
         RADIO_HW_CAPABILITY_FLAG_CAN_TX | RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO |
         RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA | RADIO_HW_CAPABILITY_FLAG_HIGH_CAPACITY;
   pModel->radioInterfacesParams.interface_supported_radio_flags[0] = RADIO_FLAGS_FRAME_TYPE_DATA |
         RADIO_FLAGS_USE_LEGACY_DATARATES | RADIO_FLAGS_USE_MCS_DATARATES | RADIO_FLAG_HT40;
   pModel->radioInterfacesParams.interface_current_frequency_khz[0] = 5800000;
   pModel->radioInterfacesParams.interface_card_model[0] = 0;

   g_pCurrentModel = pModel;

   // The radio link of the vehicle is the one of the radio interface.
   g_SM_RadioStats.radio_links[0].matchingVehicleRadioLinkId = 0;

   // The radio hardware entry: a wifi radio, opened for writing to a capture
   // file. The entry is saved, so it can be restored at the end.
   // The hardware radios are enumerated first: the enumeration fills the radio
   // list of the hardware, so it has to be done before the test entry is put
   // in place of the first one.
   hardware_get_radio_info(0);
   radio_hw_info_t* pRadios = hardware_get_radio_info_array();
   s_oSavedRadioEntry = pRadios[TEST_RADIO_INTERFACE];
   s_bRadioEntrySaved = 1;
   memset(&pRadios[TEST_RADIO_INTERFACE], 0, sizeof(radio_hw_info_t));
   pRadios[TEST_RADIO_INTERFACE].iRadioType = RADIO_TYPE_REALTEK;
   pRadios[TEST_RADIO_INTERFACE].isSupported = 1;
   pRadios[TEST_RADIO_INTERFACE].isEnabled = 1;
   pRadios[TEST_RADIO_INTERFACE].isHighCapacityInterface = 1;
   pRadios[TEST_RADIO_INTERFACE].isTxCapable = 1;
   pRadios[TEST_RADIO_INTERFACE].supportedBands = 0x0F;
   pRadios[TEST_RADIO_INTERFACE].uCurrentFrequencyKhz = 5800000;
   pRadios[TEST_RADIO_INTERFACE].phy_index = 0;
   strcpy(pRadios[TEST_RADIO_INTERFACE].szName, "testvtx0");
   strcpy(pRadios[TEST_RADIO_INTERFACE].szMAC, "001122334455");
   strcpy(pRadios[TEST_RADIO_INTERFACE].szDescription, "RubyFPV test VTX radio");

   // The video TX buffers and the video processor.
   g_pVideoTxBuffers = new VideoTxPacketsBuffer(0, 0);
   g_pVideoTxBuffers->init(pModel);
   g_pProcessorTxVideo = new ProcessorTxVideo(0, 0);
   g_pProcessorTxVideo->init();

   // The radio TX helpers: the semaphore that signals the TX power updates
   // and the thread that applies them. The thread does not touch the test
   // radio (it is not configurable), but the semaphore has to exist: the TX
   // code signals it every time the TX power changes.
   packet_utils_init();
}

static void _restore_environment()
{
   packet_utils_uninit();

   if ( s_bRadioEntrySaved )
   {
      radio_hw_info_t* pRadios = hardware_get_radio_info_array();
      pRadios[TEST_RADIO_INTERFACE] = s_oSavedRadioEntry;
      s_bRadioEntrySaved = 0;
   }
   if ( NULL != g_pVideoTxBuffers )
   {
      g_pVideoTxBuffers->uninit();
      delete g_pVideoTxBuffers;
      g_pVideoTxBuffers = NULL;
   }
   if ( NULL != g_pProcessorTxVideo )
   {
      g_pProcessorTxVideo->uninit();
      delete g_pProcessorTxVideo;
      g_pProcessorTxVideo = NULL;
   }
}

//---------------------------------------------------------------------------------
// The video input
//---------------------------------------------------------------------------------

typedef struct
{
   int iOffset;
   int iSize;
   u32 uNALPresenceFlags;
}
type_test_access_unit;

// Loads the H.264 clip and splits it into access units. An access unit starts
// with an AUD NAL unit (type 9) and it contains all the NAL units of the
// frame, each of them with its Annex-B start code.
//
// The NAL presence flags are the ones the vehicle camera code computes
// (code/r_vehicle/video_sources.cpp).
static int _load_clip_access_units(type_test_access_unit* pAUs, int iMaxAUs, u8** ppAuData)
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

   static int iNalOffsets[4096];
   static int iNalSizes[4096];
   int iNals = _split_annexb_nals(pClip, iClipSize, iNalOffsets, iNalSizes, 4096);
   if ( iNals <= 0 )
   {
      free(pClip);
      return 0;
   }

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
         pAUs[iAUs].uNALPresenceFlags = 0;
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

      // The NAL presence flags, the same way the camera code computes them.
      if ( 1 == iNalType )
         pAUs[iAUs-1].uNALPresenceFlags |= VIDEO_STATUS_FLAGS2_IS_NAL_P;
      if ( 5 == iNalType )
         pAUs[iAUs-1].uNALPresenceFlags |= VIDEO_STATUS_FLAGS2_IS_NAL_I;
      if ( parser_h264_is_signaling_nal(iNalType) )
      {
         pAUs[iAUs-1].uNALPresenceFlags |= VIDEO_STATUS_FLAGS2_IS_NAL_O;
         if ( iNalSizes[i] > 200 )
            pAUs[iAUs-1].uNALPresenceFlags |= VIDEO_STATUS_FLAGS2_IS_NAL_I;
      }
   }
   if ( iAUs > 0 )
      pAUs[iAUs-1].iSize = iAuDataPos - pAUs[iAUs-1].iOffset;
   free(pClip);

   *ppAuData = pAuData;
   return iAUs;
}

//---------------------------------------------------------------------------------
// The video TX
//---------------------------------------------------------------------------------

// Feeds the access units to the vehicle video TX code and sends the packets.
//
// The video goes in the camera input (appendDataToCurrentFrame) and the
// packets go out through the production TX path, exactly like the vehicle
// does it with the real camera.
static int _send_video(const char* szCaptureFileName, int* pCountPacketsSent)
{
   *pCountPacketsSent = 0;

   if ( 0 != radio_open_interface_for_write_to_file(TEST_RADIO_INTERFACE, szCaptureFileName) )
   {
      printf("  ERROR: the capture file %s could not be opened for writing.\n", szCaptureFileName);
      return 0;
   }

   static type_test_access_unit s_AUs[TEST_MAX_ACCESS_UNITS];
   u8* pAuData = NULL;
   int iAUs = _load_clip_access_units(s_AUs, TEST_MAX_ACCESS_UNITS, &pAuData);
   if ( iAUs <= 0 )
   {
      printf("  ERROR: the H.264 clip (res/intro.h264) could not be read.\n");
      radio_close_interface_for_write(TEST_RADIO_INTERFACE);
      return 0;
   }
   printf("  The access units of the clip: %d.\n", iAUs);

   for( int i=0; i<iAUs; i++ )
   {
      g_TimeNow = g_TimeStart + i*40;
      g_pVideoTxBuffers->appendDataToCurrentFrame(pAuData + s_AUs[i].iOffset, s_AUs[i].iSize,
            s_AUs[i].uNALPresenceFlags, true, g_TimeNow);
   }
   free(pAuData);

   // The packets are sent with the production code.
   int iCountSent = 0;
   while ( g_pVideoTxBuffers->hasPendingPacketsToSend() )
   {
      int iSent = g_pVideoTxBuffers->sendAvailablePackets(0);
      if ( iSent <= 0 )
         break;
      iCountSent += iSent;
   }
   *pCountPacketsSent = iCountSent;
   printf("  The video packets sent: %d.\n", iCountSent);

   radio_close_interface_for_write(TEST_RADIO_INTERFACE);
   return 1;
}

//---------------------------------------------------------------------------------
// The test: the normal transmission
//---------------------------------------------------------------------------------

static int _test_transmit(const char* szCaptureFileName)
{
   printf("\n--- The vehicle video transmission ---\n");

   int iCountPacketsSent = 0;
   if ( ! _send_video(szCaptureFileName, &iCountPacketsSent) )
      return 1;

   _check_true("the vehicle sent the video packets", iCountPacketsSent > 0, true);

   // The capture file is read back with the production RX code and the packets
   // are checked: they must be the production video packets and they must not
   // be marked as retransmissions.
   if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szCaptureFileName) )
   {
      _check_true("the capture file was opened for reading", false, true);
      return 1;
   }

   int iCountRead = 0;
   int iCountVideoPackets = 0;
   int iCountRetransmitted = 0;
   int iCountHighPriority = 0;
   int iCountCrcOk = 0;
   int iCountBlocks = 0;
   u32 uLastBlockIndex = 0xFFFFFFFF;
   int iFirstBlockIndex = -1;
   int iLastBlockIndex = -1;
   int iCountPacketsInFirstBlock = 0;
   bool bFirstBlock = true;

   int iPayloadLength = 0;
   while ( true )
   {
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;
      iCountRead++;

      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength <= 0) || (! bCRCOk) )
         continue;
      iCountCrcOk++;

      t_packet_header* pPH = (t_packet_header*)pPayload;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
         continue;
      if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
         continue;

      iCountVideoPackets++;
      if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
         iCountRetransmitted++;
      if ( pPH->packet_flags & PACKET_FLAGS_BIT_HIGH_PRIORITY )
         iCountHighPriority++;

      t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(pPayload + sizeof(t_packet_header));
      if ( bFirstBlock )
      {
         bFirstBlock = false;
         iFirstBlockIndex = (int)pPHVS->uCurrentBlockIndex;
         uLastBlockIndex = pPHVS->uCurrentBlockIndex;
         iLastBlockIndex = (int)pPHVS->uCurrentBlockIndex;
      }
      if ( pPHVS->uCurrentBlockIndex != uLastBlockIndex )
      {
         uLastBlockIndex = pPHVS->uCurrentBlockIndex;
         iLastBlockIndex = (int)pPHVS->uCurrentBlockIndex;
         iCountBlocks++;
      }
      if ( (int)pPHVS->uCurrentBlockIndex == iFirstBlockIndex )
         iCountPacketsInFirstBlock++;
   }

   _check("all the frames of the capture file were read", iCountRead, iCountPacketsSent);
   _check("all the packets pass the production CRC check", iCountCrcOk, iCountPacketsSent);
   _check_true("the capture file contains video packets", iCountVideoPackets > 0, true);
   _check("none of the transmitted packets is marked as a retransmission", iCountRetransmitted, 0);
   _check("none of the transmitted packets is marked as high priority", iCountHighPriority, 0);
   _check_true("the video was split in more than one block", iCountBlocks > 0, true);
   _check("the first block has all its data packets", iCountPacketsInFirstBlock, TEST_BLOCK_DATA_PACKETS);
   _check_true("the video blocks are in order", iLastBlockIndex > iFirstBlockIndex, true);

   radio_close_interface_for_read(TEST_RADIO_INTERFACE);
   return 0;
}

//---------------------------------------------------------------------------------
// The test: the answer to a retransmission request
//---------------------------------------------------------------------------------

// Reads the request of the station from the capture file with the production
// RX code and hands it over to the production vehicle code.
static int _read_request(const char* szRequestFileName)
{
   if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_UPLINK, szRequestFileName) )
   {
      printf("  ERROR: the request file %s could not be opened for reading.\n", szRequestFileName);
      return 0;
   }

   int iCountRequests = 0;
   int iCountProcessed = 0;
   int iPayloadLength = 0;
   while ( true )
   {
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;

      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength <= 0) || (! bCRCOk) )
         continue;

      t_packet_header* pPH = (t_packet_header*)pPayload;
      if ( pPH->packet_type != PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS )
         continue;
      iCountRequests++;

      // The production vehicle code that handles the request.
      process_received_single_radio_packet(TEST_RADIO_INTERFACE, pPayload, iPacketLength);
      iCountProcessed++;
   }

   radio_close_interface_for_read(TEST_RADIO_INTERFACE);
   printf("  The retransmission requests received: %d, processed: %d.\n", iCountRequests, iCountProcessed);
   return iCountProcessed;
}

static int _test_answer(const char* szRequestFileName, const char* szCaptureFileName)
{
   printf("\n--- The vehicle answers a retransmission request ---\n");

   // The video is built first: the packets of the blocks that are going to be
   // requested have to be in the TX buffer of the vehicle. The transmission
   // goes to a scratch capture file, the answer goes to the capture file of
   // this test.
   char szScratchFile[256];
   snprintf(szScratchFile, sizeof(szScratchFile), "%s.scratch", szCaptureFileName);

   int iCountPacketsSent = 0;
   if ( ! _send_video(szScratchFile, &iCountPacketsSent) )
      return 1;
   _check_true("the vehicle built and sent the video", iCountPacketsSent > 0, true);

   // The radio interface is opened again, this time for the answer.
   if ( 0 != radio_open_interface_for_write_to_file(TEST_RADIO_INTERFACE, szCaptureFileName) )
   {
      printf("  ERROR: the capture file %s could not be opened for writing.\n", szCaptureFileName);
      return 1;
   }

   // The request of the station is received and handled by the production
   // vehicle code, which sends the answer with the production TX code.
   int iCountProcessed = _read_request(szRequestFileName);
   _check("the retransmission request was handled by the vehicle code", iCountProcessed, 1);

   radio_close_interface_for_write(TEST_RADIO_INTERFACE);

   // The answer is read back and checked.
   if ( 0 == radio_open_interface_for_read_from_file(TEST_RADIO_INTERFACE, RADIO_PORT_ROUTER_DOWNLINK, szCaptureFileName) )
   {
      _check_true("the answer capture file was opened for reading", false, true);
      return 1;
   }

   int iCountVideoPackets = 0;
   int iCountRetransmitted = 0;
   int iCountHighPriority = 0;
   int iCountCrcOk = 0;
   u32 uRetrId = 0;
   bool bRetrIdFound = false;
   int iPayloadLength = 0;
   while ( true )
   {
      u8* pPayload = radio_process_wlan_data_in(TEST_RADIO_INTERFACE, &iPayloadLength, NULL, g_TimeNow);
      if ( NULL == pPayload )
         break;

      int bCRCOk = 0;
      int iPacketLength = packet_process_and_check(TEST_RADIO_INTERFACE, pPayload, iPayloadLength, &bCRCOk);
      if ( (iPacketLength <= 0) || (! bCRCOk) )
         continue;
      iCountCrcOk++;

      t_packet_header* pPH = (t_packet_header*)pPayload;
      if ( (pPH->packet_flags & PACKET_FLAGS_MASK_MODULE) != PACKET_COMPONENT_VIDEO )
         continue;
      if ( pPH->packet_type != PACKET_TYPE_VIDEO_DATA )
         continue;

      iCountVideoPackets++;
      if ( pPH->packet_flags & PACKET_FLAGS_BIT_RETRANSMITED )
         iCountRetransmitted++;
      if ( pPH->packet_flags & PACKET_FLAGS_BIT_HIGH_PRIORITY )
         iCountHighPriority++;

      t_packet_header_video_segment* pPHVS = (t_packet_header_video_segment*)(pPayload + sizeof(t_packet_header));
      if ( pPHVS->uStreamInfoFlags == VIDEO_STREAM_INFO_FLAG_RETRANSMISSION_ID )
      {
         uRetrId = pPHVS->uStreamInfo;
         bRetrIdFound = true;
      }
   }

   _check_true("the answer contains the video packet that was requested", iCountVideoPackets > 0, true);
   _check("all the packets of the answer pass the production CRC check", iCountCrcOk, iCountVideoPackets);
   _check("all the packets of the answer are marked as retransmissions", iCountRetransmitted, iCountVideoPackets);
   _check("all the packets of the answer are marked as high priority", iCountHighPriority, iCountVideoPackets);
   _check_true("the answer carries the retransmission id of the request", bRetrIdFound, true);
   _check_true("the retransmission id of the answer is not zero", uRetrId != 0, true);

   radio_close_interface_for_read(TEST_RADIO_INTERFACE);
   unlink(szScratchFile);
   return 0;
}

//---------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
   printf("================================================================\n");
   printf(" RubyFPV vehicle video TX test\n");
   printf("================================================================\n");

   if ( (argc > 2) && (0 == strcmp(argv[1], "--transmit")) )
   {
      _setup_environment();
      int iRes = _test_transmit(argv[2]);
      _restore_environment();
      printf("\n================================================================\n");
      printf(" Tests: %d, Failures: %d\n", s_iTests, s_iFailures);
      printf("================================================================\n");
      return (0 == iRes) ? ((0 == s_iFailures) ? 0 : 1) : 1;
   }

   if ( (argc > 3) && (0 == strcmp(argv[1], "--answer")) )
   {
      _setup_environment();
      int iRes = _test_answer(argv[2], argv[3]);
      _restore_environment();
      printf("\n================================================================\n");
      printf(" Tests: %d, Failures: %d\n", s_iTests, s_iFailures);
      printf("================================================================\n");
      return (0 == iRes) ? ((0 == s_iFailures) ? 0 : 1) : 1;
   }

   printf("\nUsage:\n");
   printf("  %s --transmit <capture file>\n", argv[0]);
   printf("  %s --answer <request file> <capture file>\n", argv[0]);
   return 1;
}
