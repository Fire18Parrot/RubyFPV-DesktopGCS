#include "base.h"
#include "config.h"
#include "hardware_radio_txpower.h"
#include "hardware_radio.h"
#include "hardware_procs.h"
#include <strings.h>

// Decides if a radio interface belongs to the family of a driver, so a raw
// TX power setter is applied only to the cards it is meant for.
int hardware_radio_txpower_driver_matches(int iDriverFilter, int iCardDriver, int iCardType, int bAllowRalink)
{
   // The card has to be a member of the family of the driver the command is
   // meant for. The family is checked first, so a member of the family with
   // a different driver constant is not excluded.
   bool bMatch = false;
   switch( iDriverFilter )
   {
      case RADIO_HW_DRIVER_REALTEK_8812AU:
         bMatch = hardware_radio_driver_is_rtl8812au_card(iCardDriver);
         break;
      case RADIO_HW_DRIVER_REALTEK_8812EU:
         bMatch = hardware_radio_driver_is_rtl8812eu_card(iCardDriver);
         break;
      case RADIO_HW_DRIVER_REALTEK_8733BU:
         bMatch = hardware_radio_driver_is_rtl8733bu_card(iCardDriver);
         break;
      case RADIO_HW_DRIVER_ATHEROS:
         bMatch = hardware_radio_driver_is_atheros_card(iCardDriver);
         break;
   }
   if ( ! bMatch && bAllowRalink && (iCardType == RADIO_TYPE_RALINK) )
      bMatch = true;
   return bMatch ? 1 : 0;
}


// Searches a marker in a text, ignoring the case of the letters.
//
// The error messages of the tools are not written with a fixed case ("Not
// found" and "not found" are both used), so the search has to ignore the case,
// otherwise an error with an unexpected case slips through and it is reported
// as a success.
static int _hardware_radio_output_contains(const char* szText, const char* szMarker)
{
   if ( (NULL == szText) || (NULL == szMarker) )
      return 0;

   size_t iMarkerLen = strlen(szMarker);
   if ( 0 == iMarkerLen )
      return 0;

   for( const char* p = szText; *p; p++ )
   {
      if ( 0 == strncasecmp(p, szMarker, iMarkerLen) )
         return 1;
   }

   return 0;
}


// Decides if a raw TX power command succeeded.
//
// The decision is based on the result of the command runner and on the output
// of the command, and not only on a search in the output, so a command that
// failed is never reported as a success.
int hardware_radio_txpower_command_succeeded(int iCommandResult, const char* szOutput)
{
   // 0 means the command could not be started or closed and -1 means the
   // output read timed out. Neither of them is a success.
   if ( 1 != iCommandResult )
      return 0;

   if ( NULL == szOutput )
      return 1;

   if ( _hardware_radio_output_contains(szOutput, "command failed") ||
        _hardware_radio_output_contains(szOutput, "Operation not permitted") ||
        _hardware_radio_output_contains(szOutput, "No such device") ||
        _hardware_radio_output_contains(szOutput, "not found") ||
        _hardware_radio_output_contains(szOutput, "Set failed") ||
        _hardware_radio_output_contains(szOutput, "invalid argument") ||
        _hardware_radio_output_contains(szOutput, "Usage:") )
      return 0;

   return 1;
}


// Runs an `iw` command that sets a raw TX power and reports if it worked.// The output of the command is captured, so a failure of the command (ex:
// "command failed: Operation not permitted") is detected and not only logged.
//
// iDriverFilter is the driver the command is meant for. The family helpers
// (hardware_radio_driver_is_*) decide if the card is a member of the family of
// that driver, so all the members of a family are handled, and not only the
// exact driver. If bAllowRalink is set, the Ralink cards are handled too,
// because they are configured with the same command.
static int _hardware_radio_apply_raw_txpower(int iCardIndex, int iDriverFilter, int bAllowRalink,
      const char* szFormat, int iTxPowerArgument)
{
   char szComm[256];
   char szOutput[1024];
   szOutput[0] = 0;

   int iCountApplied = 0;
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      if ( (iCardIndex != -1) && (iCardIndex != i) )
         continue;

      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;

      if ( ! hardware_radio_txpower_driver_matches(iDriverFilter, pRadioHWInfo->iRadioDriver,
            pRadioHWInfo->iRadioType, bAllowRalink) )
         continue;

      // The interface that carries the network connection of the machine is
      // never configured: it is not a radio of the ground station.
      if ( hardware_radio_is_protected_interface(i) )
      {
         log_line("Radio interface %d (%s) carries the network connection of this machine: its TX power is not changed.",
            i+1, pRadioHWInfo->szName);
         continue;
      }

      snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), szFormat, pRadioHWInfo->szName, iTxPowerArgument);
      szOutput[0] = 0;
      int iCommandResult = hw_execute_bash_command(szComm, szOutput);
      if ( ! hardware_radio_txpower_command_succeeded(iCommandResult, szOutput) )
      {
         if ( 0 == iCommandResult )
            log_softerror_and_alarm("Failed to run the command [%s] for the raw TX power of radio interface %d (%s)",
               szComm, i+1, pRadioHWInfo->szName);
         else if ( -1 == iCommandResult )
            log_softerror_and_alarm("Timed out running the command [%s] for the raw TX power of radio interface %d (%s)",
               szComm, i+1, pRadioHWInfo->szName);
         else
            log_softerror_and_alarm("Failed to set the raw TX power of radio interface %d (%s): %s",
               i+1, pRadioHWInfo->szName, szOutput);
         return 0;
      }
      iCountApplied++;
   }

   if ( 0 == iCountApplied )
      return 0;

   return 1;
}

int hardware_radio_set_txpower_raw_rtl8812au(int iCardIndex, int iTxPower)
{
   log_line("Setting radio interface %d RTL8812AU raw tx power to %d using iw dev...", iCardIndex+1, iTxPower);
   if ( (iTxPower < 1) || (iTxPower > MAX_TX_POWER) )
      iTxPower = DEFAULT_RADIO_TX_POWER;

   int iResult = _hardware_radio_apply_raw_txpower(iCardIndex, RADIO_HW_DRIVER_REALTEK_8812AU, 1,
      "iw dev %s set txpower fixed %d 2>&1", -100*iTxPower);

   // The power is changed only if the command was executed with success.
   // Reporting the change also when the command failed would be a lie.
   if ( 0 != iResult )
      log_line("Radio interface %d RTL8812AU raw tx power changed to: %d", iCardIndex+1, iTxPower);
   else
      log_line("Radio interface %d RTL8812AU raw tx power was NOT changed (it stays at %d).", iCardIndex+1, iTxPower);
   return iResult;
}

int hardware_radio_set_txpower_raw_rtl8812eu(int iCardIndex, int iTxPower)
{
   log_line("Setting radio interface %d RTL8812EU raw tx power to: %d", iCardIndex+1, iTxPower);
   if ( (iTxPower < 1) || (iTxPower > MAX_TX_POWER) )
      iTxPower = DEFAULT_RADIO_TX_POWER;

   int iResult = _hardware_radio_apply_raw_txpower(iCardIndex, RADIO_HW_DRIVER_REALTEK_8812EU, 0,
      "iw dev %s set txpower fixed %d 2>&1", iTxPower*40);

   if ( 0 != iResult )
      log_line("Radio interface %d RTL8812EU raw tx power changed to: %d", iCardIndex+1, iTxPower);
   else
      log_line("Radio interface %d RTL8812EU raw tx power was NOT changed (it stays at %d).", iCardIndex+1, iTxPower);
   return iResult;
}

int hardware_radio_set_txpower_raw_rtl8733bu(int iCardIndex, int iTxPower)
{
   log_line("Setting radio interface %d RTL8733BU raw tx power to: %d", iCardIndex+1, iTxPower);
   if ( (iTxPower < 1) || (iTxPower > MAX_TX_POWER) )
      iTxPower = DEFAULT_RADIO_TX_POWER;

   int iResult = _hardware_radio_apply_raw_txpower(iCardIndex, RADIO_HW_DRIVER_REALTEK_8733BU, 0,
      "iw dev %s set txpower fixed %d 2>&1", iTxPower*40);

   if ( 0 != iResult )
      log_line("Radio interface %d RTL8733BU raw tx power changed to: %d", iCardIndex+1, iTxPower);
   else
      log_line("Radio interface %d RTL8733BU raw tx power was NOT changed (it stays at %d).", iCardIndex+1, iTxPower);
   return iResult;
}

int hardware_radio_set_txpower_raw_atheros(int iCardIndex, int iTxPower)
{
   log_line("Setting radio interface %d Atheros raw tx power to: %d", iCardIndex+1, iTxPower);
   if ( (iTxPower < 1) || (iTxPower > MAX_TX_POWER) )
      iTxPower = DEFAULT_RADIO_TX_POWER;

   #ifdef HW_PLATFORM_RASPBERRY
   int iHasOrgFile = 0;
   if ( access( "/etc/modprobe.d/ath9k_hw.conf.org", R_OK ) != -1 )
      iHasOrgFile = 1;
   if ( iHasOrgFile )
   {
      FILE *pF = fopen("/etc/modprobe.d/ath9k_hw.conf.org", "rb");
      if ( NULL == pF )
         iHasOrgFile = 0;
      else
      {
         fseek(pF, 0, SEEK_END);
         long fsize = ftell(pF);
         fclose(pF);
         if ( fsize < 20 || fsize > 512 )
            iHasOrgFile = 0;
      }
   }

   char szBuff[256];
   szBuff[0] = 0;
   int iCountApplied = 0;
   if ( iHasOrgFile )
      sprintf(szBuff, "cp /etc/modprobe.d/ath9k_hw.conf.org tmp/ath9k_hw.conf; sed -i 's/txpower=[0-9]*/txpower=%d/g' tmp/ath9k_hw.conf; cp tmp/ath9k_hw.conf /etc/modprobe.d/", iTxPower);
   else if ( access( "/etc/modprobe.d/ath9k_hw.conf", R_OK ) != -1 )
      sprintf(szBuff, "cp /etc/modprobe.d/ath9k_hw.conf tmp/ath9k_hw.conf; sed -i 's/txpower=[0-9]*/txpower=%d/g' tmp/ath9k_hw.conf; cp tmp/ath9k_hw.conf /etc/modprobe.d/", iTxPower);
   else
      log_softerror_and_alarm("Can't access/find Atheros power config files.");

   if ( 0 != szBuff[0] )
   {
      char szOutput[512];
      szOutput[0] = 0;
      int iCommandResult = hw_execute_bash_command(szBuff, szOutput);
      // 0 means the command failed and -1 means the output read timed out.
      // Neither of them is a success.
      if ( 0 == iCommandResult )
      {
         log_softerror_and_alarm("Failed to apply the Atheros TX power configuration: %s", szOutput);
         return 0;
      }
      if ( -1 == iCommandResult )
      {
         log_softerror_and_alarm("Timed out applying the Atheros TX power configuration: %s", szOutput);
         return 0;
      }
      iCountApplied++;
   }

   // Change power for RALINK cards too. They are 2.4Ghz only cards
   if ( access( "/etc/modprobe.d/rt2800usb.conf", R_OK ) != -1 )
   {
      iTxPower = (iTxPower/10)-2;
      if ( iTxPower < 0 ) iTxPower = 0;
      if ( iTxPower > 5 ) iTxPower = 5;
      sprintf(szBuff, "cp /etc/modprobe.d/rt2800usb.conf tmp/; sed -i 's/txpower=[0-9]*/txpower=%d/g' tmp/rt2800usb.conf; cp tmp/rt2800usb.conf /etc/modprobe.d/", iTxPower);
      char szOutput[512];
      szOutput[0] = 0;
      int iCommandResult = hw_execute_bash_command(szBuff, szOutput);
      // 0 means the command failed and -1 means the output read timed out.
      if ( 0 == iCommandResult )
         log_softerror_and_alarm("Failed to apply the Ralink TX power configuration: %s", szOutput);
      else if ( -1 == iCommandResult )
         log_softerror_and_alarm("Timed out applying the Ralink TX power configuration: %s", szOutput);
      else
         iCountApplied++;
   }
   log_line("Radio interface %d Atheros raw tx power changed to: %d", iCardIndex+1, iTxPower);
   if ( 0 == iCountApplied )
   {
      log_softerror_and_alarm("Failed to apply the Atheros raw TX power %d, no configuration was changed.", iTxPower);
      return 0;
   }
   return 1;
   #else
   // On a generic Linux system the Atheros cards are configured with the
   // nl80211 interface of the driver.
   int iResult = _hardware_radio_apply_raw_txpower(iCardIndex, RADIO_HW_DRIVER_ATHEROS, 0,
      "iw dev %s set txpower fixed %d 2>&1", iTxPower*40);
   log_line("Radio interface %d Atheros raw tx power changed to: %d", iCardIndex+1, iTxPower);
   return iResult;
   #endif
}
