/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
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

#include "launchers_controller.h"
#include <pthread.h>
#include "../base/config.h"
#include "../base/hardware.h"
#include "../base/hardware_radio.h"
#include "../base/hardware_procs.h"
#include "../base/ctrl_interfaces.h"
#include "../base/ctrl_settings.h"
//#include "../base/radio_utils.h"
#include "../base/commands.h"
#include "../common/string_utils.h"

#include "shared_vars.h"
#include "popup.h"

#ifdef HW_PLATFORM_LINUX_GENERIC
// The desktop build normally has no local radio hardware, so the router
// process (which drives the radio interfaces) is not started.
// The exception is a radio that the ground station can actually use: a serial
// radio (SiK / ELRS), that talks to the radio over an ordinary serial device
// and therefore does NOT need CAP_NET_RAW nor a Wi-Fi card in monitor mode, or
// a supported Wi-Fi card, that is put in monitor mode and captured with
// libpcap. If such a radio is actually detected, the real router/station
// process has to run, otherwise no command and no video could ever reach the
// ground station.
// The same rule is used by the start script, so that the two do not
// disagree about what is expected to be running.
static int _desktop_has_usable_radio()
{
   return hardware_radio_has_usable_radios();
}
#endif

void controller_launch_router(bool bSearchMode, int iFirmwareType)
{
#ifdef HW_PLATFORM_LINUX_GENERIC
   if ( ! _desktop_has_usable_radio() )
   {
      // The desktop build has no local radio hardware, so the router process
      // (which drives the radio interfaces) is not started.
      log_line("Desktop build: not starting the controller router process.");
      return;
   }
   log_line("Desktop build: detected a usable radio, starting the controller router process.");
#endif
   log_line("Starting controller router (%s)", bSearchMode?"in search mode":"in normal mode");
   if ( hw_process_exists("ruby_rt_station") )
   {
      log_line("Controller router process already running. Do nothing.");
      return;
   }

   char szParams[256];
   char szPrefix[256];
   szPrefix[0] = 0;
   szParams[0] = 0;
   if ( bSearchMode )
   {
   
      if ( (g_iSearchSiKAirDataRate >= 0) && (iFirmwareType == MODEL_FIRMWARE_TYPE_RUBY || iFirmwareType == MODEL_FIRMWARE_TYPE_OPENIPC) )
      {
         if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRouter > 100) )
            sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRouter));
         sprintf(szParams, "-search %d -sik %d %d %d %d",
            g_iSearchFrequency, g_iSearchSiKAirDataRate, g_iSearchSiKECC, g_iSearchSiKLBT, g_iSearchSiKMCSTR);
      }
      else
      {
         if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRouter > 100) )
            sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRouter));
         sprintf(szParams, "-search %d -firmware %d", g_iSearchFrequency, iFirmwareType);
      }
   }
   else
   {
      if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRouter > 100) )
         sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRouter));
      #ifdef HW_CAPABILITY_IONICE
      if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->ioNiceRouter > 0) )
      {
         sprintf(szPrefix, "ionice -c 1 -n %d", g_pControllerSettings->ioNiceRouter);
         if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRouter > 100) )
            sprintf(szPrefix, "ionice -c 1 -n %d nice -n %d", g_pControllerSettings->ioNiceRouter, hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRouter));
      }
      #endif
   }

   if ( ! g_pControllerSettings->iPrioritiesAdjustment )
      szPrefix[0] = 0;

   hw_execute_ruby_process(szPrefix, "ruby_rt_station", szParams, NULL);
   log_line("Done launching controller router.");
}

void controller_stop_router()
{
   hw_stop_process("ruby_rt_station");
}

void controller_launch_rx_telemetry()
{
#ifdef HW_PLATFORM_LINUX_GENERIC
   // The telemetry receiver process handles the telemetry of a serial radio
   // (SiK / ELRS), that the router forwards to it. A Wi-Fi card carries the
   // telemetry in the radio link itself, handled by the router, so this
   // process is started only when there is a serial radio.
   if ( ! hardware_radio_has_serial_radios() )
   {
      // The desktop build has no local radio hardware, so the telemetry receiver
      // process is not started.
      log_line("Desktop build: not starting the rx telemetry process.");
      return;
   }
   log_line("Desktop build: detected a serial radio, starting the rx telemetry process.");
#endif
   if ( hw_process_exists("ruby_rx_telemetry") )
   {
      log_line("ruby_rx_telemetry process already running. Do nothing.");
      return;
   }
   char szPrefix[32];
   szPrefix[0] = 0;
   if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityOthers > 100) )
      sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityOthers));
   hw_execute_ruby_process(szPrefix, "ruby_rx_telemetry", NULL, NULL);
}

void controller_stop_rx_telemetry()
{
   hw_stop_process("ruby_rx_telemetry");
}

void controller_launch_tx_rc()
{
#ifdef HW_PLATFORM_LINUX_GENERIC
   // The desktop build has no local radio hardware, so the RC transmitter
   // process is not started.
   log_line("Desktop build: not starting the tx rc process.");
   return;
#else
   if ( hw_process_exists("ruby_tx_rc") )
   {
      log_line("ruby_tx_rc process already running. Do nothing.");
      return;
   }

   char szPrefix[64];
   char szParams[32];
   szPrefix[0] = 0;
   szParams[0] = 0;

   if ( g_bSearching )
      sprintf(szParams, "-search");

   if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRC > 100) )
      sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRC));

   #ifdef HW_CAPABILITY_IONICE
   if ( g_pControllerSettings->iPrioritiesAdjustment && (NULL != g_pCurrentModel) )
   if ( DEFAULT_IO_PRIORITY_RC > 0 )
   {
      sprintf(szPrefix, "ionice -c 1 -n %d", DEFAULT_IO_PRIORITY_RC);
      if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityRC > 100) )
         sprintf(szPrefix, "ionice -c 1 -n %d nice -n %d", DEFAULT_IO_PRIORITY_RC, hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityRC));
   }
   #endif

   hw_execute_ruby_process(szPrefix, "ruby_tx_rc", szParams, NULL);
#endif
}

void controller_stop_tx_rc()
{
   hw_stop_process("ruby_tx_rc");
}


static const char* s_szControllerCardErrorFrequency = "You can't set frequency %s, it's used by a different radio link. You would have two radio interfaces transmitting on the same frequency.";
static char s_szControllerCardError[1024];

const char* controller_validate_radio_settings(Model* pModel, u32* pVehicleNICFreq, int* pVehicleNICFlags, int* pVehicleNICRadioFlags, int* pControllerNICFlags, int* pControllerNICRadioFlags)
{
   s_szControllerCardError[0] = 0;

   // Check frequencies

   if ( NULL != pModel && pModel->radioInterfacesParams.interfaces_count > 1 )
   if ( NULL != pVehicleNICFreq )
   {
      for( int i=0; i<pModel->radioInterfacesParams.interfaces_count-1; i++ )
      {
         for( int k=i+1; k<pModel->radioInterfacesParams.interfaces_count; k++ )
         {
            if ( NULL == pVehicleNICFlags )
               continue;
            if ( pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
               continue;
            if ( pVehicleNICFlags[k] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
               continue;

            u32 uFreq1 = pVehicleNICFreq[i];
            u32 uFreq2 = pVehicleNICFreq[k];

            if ( uFreq1 == uFreq2 )
            {
               sprintf(s_szControllerCardError, s_szControllerCardErrorFrequency, str_format_frequency(uFreq1));
               return s_szControllerCardError;
            }
            
            if ( pModel->relay_params.isRelayEnabledOnRadioLinkId >= 0 )
            if ( pModel->relay_params.uRelayedVehicleId != 0 )
            if ( pModel->relay_params.uRelayedVehicleId != MAX_U32 )
            if ( pModel->relay_params.uRelayFrequencyKhz != 0 )
            if ( uFreq1 == pModel->relay_params.uRelayFrequencyKhz )
            {
               sprintf(s_szControllerCardError, "You can't set frequency %s, it's used for relaying.", str_format_frequency(uFreq1));
               return s_szControllerCardError;
            }        
         }
      }
   }
   

   // Check [all cards disabled] on vehicle

   if ( NULL != pModel && NULL != pVehicleNICFlags )
   {
      bool bAllDisabled = true;
      for( int i=0; i<pModel->radioInterfacesParams.interfaces_count; i++ )
         if ( !(pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED) )
            bAllDisabled = false;
      if ( bAllDisabled )
      {
         strcpy( s_szControllerCardError, "You can't disable all radio links on the vehicle.");
         return s_szControllerCardError;
      }
   }

   // Check missing data capable cards on vehicle

   if ( (NULL != pModel) && (NULL != pVehicleNICFlags) )
   {
      bool bHasData = false;
      for( int i=0; i<pModel->radioInterfacesParams.interfaces_count; i++ )
      {
         if ( pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_DISABLED )
            continue;
         if ( pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_USED_FOR_RELAY )
            continue;
         //if ( pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_VIDEO )
         //   bHasVideo = true;
         if ( pVehicleNICFlags[i] & RADIO_HW_CAPABILITY_FLAG_CAN_USE_FOR_DATA )
            bHasData = true;
      }
      if ( ! bHasData )
      {
         strcpy( s_szControllerCardError, "You can't remove all data links from the vehicle.");
         return s_szControllerCardError;
      }
   }

   return NULL;
}


void controller_start_i2c()
{
#ifdef HW_PLATFORM_LINUX_GENERIC
   // The desktop build has no local I2C bus, so the I2C helper process is
   // not started.
   log_line("[Launcher] Desktop build: not starting the I2C helper process.");
   return;
#else
   char szPrefix[64];
   szPrefix[0] = 0;

   if ( g_pControllerSettings->iPrioritiesAdjustment && (g_pControllerSettings->iThreadPriorityOthers > 100) )
      sprintf(szPrefix, "nice -n %d", hw_get_nice_value_for_raw_priority(g_pControllerSettings->iThreadPriorityOthers));
   hw_execute_ruby_process(szPrefix, "ruby_i2c", NULL, NULL);
#endif
}

void controller_stop_i2c()
{
   hw_stop_process("ruby_i2c");
}

bool _controller_wait_for_stop_process(const char* szProcName)
{
   char szPIDs[1024];

   if ( NULL == szProcName || 0 == szProcName[0] )
      return false;

   int retryCount = 40;
   while ( retryCount > 0 )
   {
      hardware_sleep_ms(10);
      szPIDs[0] = 0;
      hw_process_get_pids(szProcName, szPIDs);
      removeTrailingNewLines(szPIDs);
      if ( (0 == szPIDs[0]) || (strlen(szPIDs) < 2) )
      {
         log_line("Process %s has finished and exited.", szProcName);
         return true;
      }
      hardware_sleep_ms(10);
      retryCount--;
   }
   log_softerror_and_alarm("Process %s failed to exit after 3 seconds.", szProcName);
   return false;
}

void controller_wait_for_stop_all()
{
   log_line("Waiting for all pairing processes to stop...");

   if ( ! _controller_wait_for_stop_process("ruby_rx_telemetry") )
      log_softerror_and_alarm("Failed to wait for stopping: ruby_rx_telemetry");

   if ( ! _controller_wait_for_stop_process("ruby_tx_rc") )
      log_softerror_and_alarm("Failed to wait for stopping: ruby_tx_rc");

   // The name of the router process is ruby_rt_station, the same name that is
   // used to start it and to stop it. Waiting for a process that does not
   // exist would report a failure that is not real.
   if ( ! _controller_wait_for_stop_process("ruby_rt_station") )
      log_softerror_and_alarm("Failed to wait for stopping: ruby_rt_station");

   log_line("All pairing processes have finished and exited.");
}
