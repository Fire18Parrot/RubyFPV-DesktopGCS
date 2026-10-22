/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source code and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code (partially or complete) must retain
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

// SiK serial radio path regression test.
//
// The test runs a SiK radio protocol emulator on a pseudo terminal (a PTY)
// and drives the REAL production code:
//
//   hardware_radio_sik_detect_interfaces()      (the radio detection)
//     -> hardware_radio_sik_try_detect_on_port()
//       -> _hardware_radio_sik_get_all_params() (the AT command exchange)
//   hardware_radio_sik_open_for_read_write()    (the open of the radio)
//   hardware_radio_sik_write_packet()           (the write of a radio packet)
//   radio_rx_start_rx_thread()                  (the production RX thread)
//     -> _radio_rx_parse_received_serial_radio_data()
//       -> radio_buffer_is_valid_short_packet()
//       -> _radio_rx_process_serial_short_packet()
//         -> _radio_rx_check_add_packet_to_rx_queue()
//
// The emulator loops the written data back, the same way a radio that is
// connected to a second one would do it. This is a controlled loopback over a
// pseudo terminal, it is NOT real-RF evidence.
//
// The radio device is given to the production code with the RUBY_SIK_PORTS
// environment variable, so that the test never touches a real radio and never
// probes the serial devices of the machine.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <pty.h>
#include <termios.h>
#include <vector>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_file_names.h"
#include "../base/hardware.h"
#include "../base/hardware_radio.h"
#include "../base/hardware_radio_sik.h"
#include "../base/hardware_serial.h"
#include "../base/shared_mem_radio.h"
#include "../radio/radio_rx.h"
#include "../radio/radiopackets2.h"
#include "../radio/radiopackets_short.h"

static int s_iFailures = 0;
static int s_iChecks = 0;

static void check(const char* szName, int iGot, int iExpected)
{
   s_iChecks++;
   if ( iGot == iExpected )
      printf("  PASS  %-58s got=%d expect=%d\n", szName, iGot, iExpected);
   else
   {
      printf("  FAIL  %-58s got=%d expect=%d\n", szName, iGot, iExpected);
      s_iFailures++;
   }
}

// --- The SiK radio emulator ---

// The emulator answers the AT commands of the production detection and it
// loops the data that is written in data mode back to the reader.
//
// The command mode is entered with the "+++" sequence, the same way a real
// SiK radio does it. The answers have the format of the SiK radios: the 0x16
// byte, the text and an empty line at the end.
class SikEmulator
{
   public:
      SikEmulator() : m_iMasterFD(-1), m_bRunning(false), m_bCommandMode(false), m_iPlusCount(0) {}

      bool start(int iMasterFD)
      {
         m_iMasterFD = iMasterFD;
         m_bRunning = true;
         return (0 == pthread_create(&m_Thread, NULL, &SikEmulator::_thread_proc, this));
      }

      void stop()
      {
         m_bRunning = false;
         pthread_join(m_Thread, NULL);
      }

      // The bytes that were received in data mode, the data that was looped back.
      int get_received_data(u8* pBuffer, int iMaxLength)
      {
         pthread_mutex_lock(&m_Mutex);
         int iLength = (int)m_ReceivedData.size();
         if ( iLength > iMaxLength )
            iLength = iMaxLength;
         if ( iLength > 0 )
            memcpy(pBuffer, m_ReceivedData.data(), iLength);
         pthread_mutex_unlock(&m_Mutex);
         return iLength;
      }

   private:
      static void* _thread_proc(void* pArg)
      {
         SikEmulator* pEmu = (SikEmulator*)pArg;
         pEmu->_run();
         return NULL;
      }

      void _write(const char* szData, int iLength)
      {
         int iWritten = 0;
         while ( iWritten < iLength )
         {
            int iRes = write(m_iMasterFD, szData+iWritten, iLength-iWritten);
            if ( iRes <= 0 )
               break;
            iWritten += iRes;
         }
      }

      // Sends the answer of a command, in the format of the SiK radios.
      void _answer(const char* szText)
      {
         char szBuffer[512];
         int iLength = 0;
         szBuffer[iLength++] = 0x16;
         szBuffer[iLength++] = '\r';
         szBuffer[iLength++] = '\n';
         for( int i=0; szText[i] != 0; i++ )
            szBuffer[iLength++] = szText[i];
         szBuffer[iLength++] = '\r';
         szBuffer[iLength++] = '\n';
         szBuffer[iLength++] = '\r';
         szBuffer[iLength++] = '\n';
         _write(szBuffer, iLength);
      }

      void _process_command(const char* szCommand)
      {
         if ( 0 == strcmp(szCommand, "ATI") )
         {
            _answer("SiK 2.2.4");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATI1") )
         {
            _answer("1234");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATI2") )
         {
            _answer("5678");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATI3") )
         {
            _answer("9ABC");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATI4") )
         {
            _answer("DEF0");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATS8?") )
         {
            // The frequency is reported in Hz, it is the 868 MHz band
            _answer("868000000");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATS9?") )
         {
            _answer("869000000");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATS2?") )
         {
            // The air speed, it is the index 57 in the SiK baud rate table
            _answer("57");
            return;
         }
         if ( 0 == strcmp(szCommand, "ATO") )
         {
            _answer("OK");
            m_bCommandMode = false;
            return;
         }
         // Any other parameter is reported as OK, the production code only
         // checks that the radio answered.
         _answer("OK");
      }

      void _run()
      {
         u8 uBuffer[1024];
         char szCommand[256];
         int iCommandLength = 0;
         int iPlusCount = 0;

         while( m_bRunning )
         {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(m_iMasterFD, &readSet);
            struct timeval to;
            to.tv_sec = 0;
            to.tv_usec = 100000;

            int iRes = select(m_iMasterFD+1, &readSet, NULL, NULL, &to);
            if ( iRes <= 0 )
               continue;

            int iRead = read(m_iMasterFD, uBuffer, sizeof(uBuffer));
            if ( iRead <= 0 )
            {
               if ( (iRead < 0) && (errno == EINTR) )
                  continue;
               break;
            }

            for( int i=0; i<iRead; i++ )
            {
               u8 uByte = uBuffer[i];

               if ( ! m_bCommandMode )
               {
                  // The command mode is entered with the "+++" sequence. The
                  // sequence is not looped back, a real radio does not echo
                  // it in data mode.
                  if ( uByte == '+' )
                  {
                     iPlusCount++;
                     if ( iPlusCount == 3 )
                     {
                        iPlusCount = 0;
                        m_bCommandMode = true;
                        iCommandLength = 0;
                        _write("OK\r\n", 5);
                     }
                     continue;
                  }
                  iPlusCount = 0;

                  // Data mode: the bytes are looped back to the reader.
                  pthread_mutex_lock(&m_Mutex);
                  m_ReceivedData.push_back(uByte);
                  pthread_mutex_unlock(&m_Mutex);
                  _write((const char*)&uByte, 1);
                  continue;
               }

               // Command mode: the commands are terminated by a carriage
               // return.
               if ( (uByte == '\r') || (uByte == '\n') )
               {
                  szCommand[iCommandLength] = 0;
                  iCommandLength = 0;
                  if ( szCommand[0] != 0 )
                     _process_command(szCommand);
                  continue;
               }
               if ( iCommandLength < (int)sizeof(szCommand)-1 )
               {
                  szCommand[iCommandLength++] = (char)uByte;
                  szCommand[iCommandLength] = 0;
               }
            }
         }
      }

      int m_iMasterFD;
      bool m_bRunning;
      bool m_bCommandMode;
      int m_iPlusCount;
      pthread_t m_Thread;
      pthread_mutex_t m_Mutex = PTHREAD_MUTEX_INITIALIZER;
      std::vector<u8> m_ReceivedData;
};

// --- The test ---

// Saves the content of a file, so that the test can restore it. The test
// writes the configuration of the detected radios, that configuration is the
// one of the application.
class SavedFile
{
   public:
      SavedFile(const char* szPath) : m_szPath(szPath), m_bExisted(false), m_iLength(0)
      {
         FILE* fd = fopen(szPath, "rb");
         if ( NULL == fd )
            return;
         m_bExisted = true;
         fseek(fd, 0, SEEK_END);
         long lSize = ftell(fd);
         fseek(fd, 0, SEEK_SET);
         if ( lSize > 0 )
         {
            m_pData = (u8*)malloc(lSize);
            m_iLength = (int)fread(m_pData, 1, lSize, fd);
         }
         fclose(fd);
      }

      ~SavedFile()
      {
         if ( NULL != m_pData )
            free(m_pData);
      }

      void restore()
      {
         if ( ! m_bExisted )
         {
            unlink(m_szPath);
            return;
         }
         FILE* fd = fopen(m_szPath, "wb");
         if ( NULL == fd )
            return;
         if ( m_iLength > 0 )
            fwrite(m_pData, 1, m_iLength, fd);
         fclose(fd);
      }

   private:
      const char* m_szPath;
      bool m_bExisted;
      int m_iLength;
      u8* m_pData = NULL;
};

int main()
{
   printf("== SiK serial radio path test (PTY loopback) ==\n");

   char szConfigFile[512];
   snprintf(szConfigFile, sizeof(szConfigFile)/sizeof(szConfigFile[0]), "%s%s", FOLDER_CONFIG, FILE_CONFIG_LAST_SIK_RADIOS_DETECTED);
   SavedFile oSavedConfig(szConfigFile);

   // The test adds the emulated radio to the serial ports of the
   // application, the configuration of the serial ports is saved and restored
   // by the test, so that the configuration of the machine is not changed.
   char szSerialConfigFile[512];
   snprintf(szSerialConfigFile, sizeof(szSerialConfigFile)/sizeof(szSerialConfigFile[0]), "%s%s", FOLDER_CONFIG, FILE_CONFIG_HW_SERIAL_PORTS);
   SavedFile oSavedSerialConfig(szSerialConfigFile);

   // The pseudo terminal: the slave is the device that the production code
   // opens as a serial port, the master is the side of the emulator.
   int iMasterFD = -1;
   int iSlaveFD = -1;
   if ( 0 != openpty(&iMasterFD, &iSlaveFD, NULL, NULL, NULL) )
   {
      printf("  FAIL  could not create the pseudo terminal, error: %s\n", strerror(errno));
      return 1;
   }

   // The name of the pseudo terminal is read from the master side, that is
   // the side that has the device name of the slave.
   char szDeviceName[256];
   szDeviceName[0] = 0;
   if ( 0 != ptsname_r(iMasterFD, szDeviceName, sizeof(szDeviceName)) )
   {
      printf("  FAIL  could not get the name of the pseudo terminal, error: %s\n", strerror(errno));
      return 1;
   }

   // The production code opens the device as a raw serial port.
   struct termios oOptions;
   if ( 0 == tcgetattr(iSlaveFD, &oOptions) )
   {
      cfmakeraw(&oOptions);
      tcsetattr(iSlaveFD, TCSANOW, &oOptions);
   }

   printf("STEP0: the emulated radio is on the device %s\n", szDeviceName);

   // The radio device is given to the production code, so that the test never
   // probes the serial devices of the machine.
   setenv("RUBY_SIK_PORTS", szDeviceName, 1);

   SikEmulator oEmulator;
   if ( ! oEmulator.start(iMasterFD) )
   {
      printf("  FAIL  could not start the emulator thread\n");
      return 1;
   }

   // 1) The production detection has to find the emulated radio
   printf("STEP1 DETECT: running the production radio detection\n");
   hardware_radio_sik_detect_interfaces();

   int iCount = hardware_get_radio_interfaces_count();
   int iSikIndex = -1;
   for( int i=0; i<iCount; i++ )
   {
      radio_hw_info_t* pInfo = hardware_get_radio_info(i);
      if ( NULL == pInfo )
         continue;
      printf("STEP1 DETECT: interface %d: name=[%s] driver=[%s] MAC=[%s] type=%d serial=%d supported=%d\n",
         i, pInfo->szName, pInfo->szDriver, pInfo->szMAC, pInfo->iRadioType, pInfo->isSerialRadio, pInfo->isSupported);
      if ( pInfo->isSerialRadio && (RADIO_TYPE_SIK == pInfo->iRadioType) )
         iSikIndex = i;
   }
   check("the production detection found the SiK radio", (iSikIndex >= 0) ? 1 : 0, 1);

   if ( iSikIndex < 0 )
   {
      oEmulator.stop();
      oSavedConfig.restore();
      oSavedSerialConfig.restore();
      return 1;
   }

   radio_hw_info_t* pRadioInfo = hardware_get_radio_info(iSikIndex);
   check("the detected radio is on the emulated device",
      (0 == strcmp(pRadioInfo->szDriver, szDeviceName)) ? 1 : 0, 1);
   check("the detected radio is supported", pRadioInfo->isSupported, 1);
   check("the detected radio has a MAC address", (0 != pRadioInfo->szMAC[0]) ? 1 : 0, 1);
   check("the detected radio is in the 868 MHz band",
      (pRadioInfo->supportedBands & RADIO_HW_SUPPORTED_BAND_868) ? 1 : 0, 1);
   check("the detected radio is a serial radio", pRadioInfo->isSerialRadio, 1);

   // The device of the radio has to be in the serial ports, so that it is
   // visible in the settings of the application.
   hw_serial_port_info_t* pSerialPort = hardware_get_serial_port_info_from_serial_port_name(pRadioInfo->szDriver);
   check("the device of the radio is in the serial ports", (NULL != pSerialPort) ? 1 : 0, 1);
   if ( NULL != pSerialPort )
      check("the device of the radio is marked as a radio port", pSerialPort->iPortUsage, SERIAL_PORT_USAGE_SIK_RADIO);

   // 2) The production open path
   printf("STEP2 OPEN: opening the radio for read and write\n");
   int iOpened = hardware_radio_sik_open_for_read_write(iSikIndex);
   printf("STEP2 OPEN: hardware_radio_sik_open_for_read_write() = %d\n", iOpened);
   check("the production open path succeeded", (0 < iOpened) ? 1 : 0, 1);
   check("the radio is opened for read", pRadioInfo->openedForRead, 1);
   check("the radio is opened for write", pRadioInfo->openedForWrite, 1);

   if ( 0 >= iOpened )
   {
      oEmulator.stop();
      oSavedConfig.restore();
      oSavedSerialConfig.restore();
      return 1;
   }

   // 3) The production RX thread
   printf("STEP3 RX: starting the production RX thread\n");
   shared_mem_radio_stats sStats;
   memset(&sStats, 0, sizeof(sStats));
   int iRxStarted = radio_rx_start_rx_thread(&sStats, 0, 0);
   check("the production RX thread was started", iRxStarted, 1);

   // 4) A radio packet, built with the production packet builder, so that the
   //    CRC of the packet is valid
   u8 uFullPacket[128];
   memset(uFullPacket, 0, sizeof(uFullPacket));
   int iFullLength = 48;
   radio_packet_init((t_packet_header*)uFullPacket, 0, 1, 0);
   t_packet_header* pPH = (t_packet_header*)uFullPacket;
   pPH->total_length = iFullLength;
   pPH->vehicle_id_src = 1;
   pPH->stream_packet_idx = 1;
   pPH->packet_flags |= PACKET_FLAGS_BIT_HIGH_PRIORITY;
   for( int i=(int)sizeof(t_packet_header); i<iFullLength; i++ )
      uFullPacket[i] = (u8)(0x20 + i);
   radio_packet_compute_crc(uFullPacket, iFullLength);
   check("radio_packet_check_crc() accepts the built packet",
      radio_packet_check_crc(uFullPacket, iFullLength), 1);

   // 5) The short packet that carries the radio packet over the serial link
   u8 uShortPacket[256];
   memset(uShortPacket, 0, sizeof(uShortPacket));
   t_packet_header_short* pPHS = (t_packet_header_short*)uShortPacket;
   radio_packet_short_init(pPHS);
   pPHS->start_header = SHORT_PACKET_START_BYTE_START_PACKET;
   pPHS->packet_id = 1;
   pPHS->data_length = (u8)iFullLength;
   memcpy(uShortPacket + sizeof(t_packet_header_short), uFullPacket, iFullLength);
   pPHS->crc = base_compute_crc8(uShortPacket+2, pPHS->data_length + sizeof(t_packet_header_short)-2);
   int iShortLength = (int)(sizeof(t_packet_header_short) + iFullLength);
   check("radio_buffer_is_valid_short_packet() accepts the built short packet",
      radio_buffer_is_valid_short_packet(uShortPacket, iShortLength), 1);

   // 6) The production write path
   printf("STEP4 WRITE: writing the short packet with the production SiK write path\n");
   int iWritten = hardware_radio_sik_write_packet(iSikIndex, uShortPacket, iShortLength);
   printf("STEP4 WRITE: hardware_radio_sik_write_packet() = %d\n", iWritten);
   check("the production SiK write succeeded", iWritten, 1);

   // 7) The emulator has to receive the written bytes
   u8 uReceived[512];
   int iReceived = 0;
   for( int i=0; i<40; i++ )
   {
      iReceived = oEmulator.get_received_data(uReceived, sizeof(uReceived));
      if ( iReceived >= iShortLength )
         break;
      usleep(50000);
   }
   check("the emulator received the written bytes", (iReceived >= iShortLength) ? 1 : 0, 1);
   if ( iReceived >= iShortLength )
   {
      check("the received bytes are the written ones",
         (0 == memcmp(uReceived, uShortPacket, iShortLength)) ? 1 : 0, 1);
   }

   // 8) The production RX thread has to deliver the packet back
   int iGot = 0;
   int iGotLength = 0;
   u8* pGot = NULL;
   for( int i=0; i<60; i++ )
   {
      int iLength = 0;
      int iIsShort = 0;
      int iInterface = -1;
      pGot = radio_rx_wait_get_next_received_high_prio_packet(100000, &iLength, &iIsShort, &iInterface);
      if ( NULL != pGot )
      {
         iGot = 1;
         iGotLength = iLength;
         printf("STEP5 RX: got a packet, length=%d isShort=%d interface=%d\n", iLength, iIsShort, iInterface);
         break;
      }
      usleep(50000);
   }
   check("the production RX thread delivered the looped-back packet", iGot, 1);
   if ( 0 < iGot )
   {
      check("the delivered packet length matches the transmitted one", iGotLength, iFullLength);
      int iSame = (iGotLength == iFullLength) && (0 == memcmp(pGot, uFullPacket, iFullLength));
      check("the delivered packet payload matches what was transmitted", iSame, 1);
   }
   check("the link statistics counted the received packet",
      (0 < sStats.radio_interfaces[iSikIndex].totalRxPackets) ? 1 : 0, 1);
   check("the link statistics counted no bad packet", sStats.radio_interfaces[iSikIndex].totalRxPacketsBad, 0);

   // 9) Close
   printf("STEP6 CLOSE: closing the radio interface\n");
   radio_rx_stop_rx_thread();
   int iClosed = hardware_radio_sik_close(iSikIndex);
   check("the production close path succeeded", iClosed, 1);
   check("the radio is not opened for read anymore", pRadioInfo->openedForRead, 0);
   check("the radio is not opened for write anymore", pRadioInfo->openedForWrite, 0);

   oEmulator.stop();
   close(iSlaveFD);
   close(iMasterFD);
   oSavedConfig.restore();
   oSavedSerialConfig.restore();

   printf("\n=========================================================\n");
   printf("SiK SERIAL PATH TEST: %d checks, %d failure(s)\n", s_iChecks, s_iFailures);
   printf("=========================================================\n");
   return s_iFailures;
}
