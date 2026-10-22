/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use in source and/or binary forms (partially or complete) must retain
          the above copyright notice, this list of conditions and the following disclaimer
          in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form must reproduce the above copyright notice,
          this list of conditions and the following disclaimer in the documentation and/or other
          materials provided with the distribution.
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

#include "base.h"
#include "config.h"
#include "config_file_names.h"
#include "config_radio.h"
#include "hardware_radio.h"
#include "hardware_radio_txpower.h"
#include "hardware_procs.h"
#include "radio_capabilities.h"
#include "radio_profiles.h"
#include "tx_powers.h"
#include "utils.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static radio_profile_t s_Profiles[RADIO_PROFILE_MAX_COUNT];
static int s_iProfilesCount = 0;
static int s_bProfilesLoaded = 0;


int radio_profiles_init()
{
   memset(s_Profiles, 0, sizeof(s_Profiles));
   s_iProfilesCount = 0;
   s_bProfilesLoaded = 0;
   return 1;
}

int radio_profiles_get_count()
{
   return s_iProfilesCount;
}

const radio_profile_t* radio_profiles_get(int iIndex)
{
   if ( (iIndex < 0) || (iIndex >= s_iProfilesCount) )
      return NULL;
   return &(s_Profiles[iIndex]);
}

int radio_profiles_find_by_name(const char* szName)
{
   if ( NULL == szName )
      return -1;
   for( int i=0; i<s_iProfilesCount; i++ )
      if ( 0 == strcmp(s_Profiles[i].szName, szName) )
         return i;
   return -1;
}

int radio_profiles_add(const radio_profile_t* pProfile)
{
   if ( NULL == pProfile )
      return -1;
   if ( s_iProfilesCount >= RADIO_PROFILE_MAX_COUNT )
   {
      log_softerror_and_alarm("Cannot add RF profile: the maximum number of profiles (%d) was reached.", RADIO_PROFILE_MAX_COUNT);
      return -1;
   }
   if ( pProfile->szName[0] == 0 )
   {
      log_softerror_and_alarm("Cannot add RF profile: the profile name is empty.");
      return -1;
   }
   if ( radio_profiles_find_by_name(pProfile->szName) >= 0 )
   {
      log_softerror_and_alarm("Cannot add RF profile: a profile named [%s] already exists.", pProfile->szName);
      return -1;
   }
   memcpy(&(s_Profiles[s_iProfilesCount]), pProfile, sizeof(radio_profile_t));
   s_Profiles[s_iProfilesCount].szName[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
   s_iProfilesCount++;
   return s_iProfilesCount-1;
}

int radio_profiles_update(int iIndex, const radio_profile_t* pProfile)
{
   if ( (NULL == pProfile) || (iIndex < 0) || (iIndex >= s_iProfilesCount) )
      return 0;
   if ( pProfile->szName[0] == 0 )
   {
      log_softerror_and_alarm("Cannot update RF profile: the profile name is empty.");
      return 0;
   }
   int iOther = radio_profiles_find_by_name(pProfile->szName);
   if ( (iOther >= 0) && (iOther != iIndex) )
   {
      log_softerror_and_alarm("Cannot update RF profile: a profile named [%s] already exists.", pProfile->szName);
      return 0;
   }
   memcpy(&(s_Profiles[iIndex]), pProfile, sizeof(radio_profile_t));
   s_Profiles[iIndex].szName[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
   return 1;
}

int radio_profiles_remove(int iIndex)
{
   if ( (iIndex < 0) || (iIndex >= s_iProfilesCount) )
      return 0;
   for( int i=iIndex; i<s_iProfilesCount-1; i++ )
      memcpy(&(s_Profiles[i]), &(s_Profiles[i+1]), sizeof(radio_profile_t));
   memset(&(s_Profiles[s_iProfilesCount-1]), 0, sizeof(radio_profile_t));
   s_iProfilesCount--;
   return 1;
}

int radio_profiles_duplicate(int iIndex, const char* szNewName)
{
   if ( (iIndex < 0) || (iIndex >= s_iProfilesCount) )
      return -1;
   if ( s_iProfilesCount >= RADIO_PROFILE_MAX_COUNT )
   {
      log_softerror_and_alarm("Cannot duplicate RF profile: the maximum number of profiles (%d) was reached.", RADIO_PROFILE_MAX_COUNT);
      return -1;
   }

   radio_profile_t Profile;
   memcpy(&Profile, &(s_Profiles[iIndex]), sizeof(radio_profile_t));

   if ( (NULL != szNewName) && (szNewName[0] != 0) )
   {
      strncpy(Profile.szName, szNewName, RADIO_PROFILE_MAX_NAME_LENGTH);
      Profile.szName[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
   }
   else
   {
      // build a unique name: "<name> copy", "<name> copy 2", ...
      char szBase[RADIO_PROFILE_MAX_NAME_LENGTH+8];
      strncpy(szBase, Profile.szName, sizeof(szBase)-1);
      szBase[sizeof(szBase)-1] = 0;
      int iCopy = 1;
      while( true )
      {
         if ( 1 == iCopy )
            snprintf(Profile.szName, sizeof(Profile.szName), "%s copy", szBase);
         else
            snprintf(Profile.szName, sizeof(Profile.szName), "%s copy %d", szBase, iCopy);
         Profile.szName[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
         if ( radio_profiles_find_by_name(Profile.szName) < 0 )
            break;
         iCopy++;
         if ( iCopy > 100 )
            return -1;
      }
   }

   return radio_profiles_add(&Profile);
}


const char* radio_profiles_get_file_path()
{
   static char s_szPath[MAX_FILE_PATH_SIZE];
   // The path can be overridden with an environment variable, so the tests can
   // use a temporary file instead of the real configuration file.
   const char* szOverride = getenv("RUBY_RF_PROFILES_FILE");
   if ( (NULL != szOverride) && (0 != strlen(szOverride)) )
   {
      strncpy(s_szPath, szOverride, sizeof(s_szPath)-1);
      s_szPath[sizeof(s_szPath)-1] = 0;
      return s_szPath;
   }
   strncpy(s_szPath, FOLDER_CONFIG, sizeof(s_szPath)-1);
   s_szPath[sizeof(s_szPath)-1] = 0;
   strncat(s_szPath, FILE_CONFIG_RF_PROFILES, sizeof(s_szPath)-strlen(s_szPath)-1);
   s_szPath[sizeof(s_szPath)-1] = 0;
   return s_szPath;
}


// The RF profiles file has the following layout:
//   u32 uFileSize   - the size of the file, in bytes
//   u32 uVersion    - the format version
//   u32 uCount      - the number of profiles
//   radio_profile_t Profiles[uCount]
//   u32 uCrc        - the CRC32 of everything between the size and the CRC
// Sanitizes the values of a loaded profile, so a corrupted file can not crash
// the UI and can not produce an invalid configuration.
static void _radio_profiles_sanitize(radio_profile_t* pProfile, int iIndex)
{
   if ( NULL == pProfile )
      return;
   pProfile->szName[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
   if ( (pProfile->iRole < 0) || (pProfile->iRole >= RADIO_CAP_ROLE_COUNT) )
      pProfile->iRole = RADIO_CAP_ROLE_TXRX;
   if ( (pProfile->iMode < 0) || (pProfile->iMode >= RADIO_CAP_MODE_COUNT) )
      pProfile->iMode = RADIO_CAP_MODE_STATION;
   if ( (pProfile->iBandwidthMode < 0) || (pProfile->iBandwidthMode >= RADIO_CAP_BW_COUNT) )
      pProfile->iBandwidthMode = RADIO_CAP_BW_20MHZ;
   if ( pProfile->iConfiguredTxPowerDbm < 0 )
      pProfile->iConfiguredTxPowerDbm = 0;
   if ( pProfile->iProfileMaxTxPowerDbm < 0 )
      pProfile->iProfileMaxTxPowerDbm = 0;
   if ( pProfile->iInterfaceIndex < RADIO_PROFILE_IFACE_AUTO )
      pProfile->iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   if ( pProfile->iEnabled < 0 )
      pProfile->iEnabled = 0;
   if ( pProfile->iChannel < 0 )
      pProfile->iChannel = 0;
   if ( pProfile->iBand < 0 )
      pProfile->iBand = 0;
   if ( pProfile->szName[0] == 0 )
      snprintf(pProfile->szName, sizeof(pProfile->szName), "Profile %d", iIndex+1);
}


int radio_profiles_load()
{
   const char* szFile = radio_profiles_get_file_path();
   memset(s_Profiles, 0, sizeof(s_Profiles));
   s_iProfilesCount = 0;

   FILE* fp = fopen(szFile, "rb");
   if ( NULL == fp )
   {
      log_line("No RF profiles file (%s). Starting with no profiles.", szFile);
      s_bProfilesLoaded = 1;
      return 0;
   }

   // read the whole file, it is small
   fseek(fp, 0, SEEK_END);
   long lFileSize = ftell(fp);
   fseek(fp, 0, SEEK_SET);
   if ( lFileSize < (long)(3*sizeof(u32)) )
   {
      log_softerror_and_alarm("The RF profiles file %s is too small (%ld bytes). Ignoring the file.", szFile, lFileSize);
      fclose(fp);
      s_bProfilesLoaded = 1;
      return 0;
   }

   u8* pBuffer = (u8*)malloc((size_t)lFileSize);
   if ( NULL == pBuffer )
   {
      log_softerror_and_alarm("Failed to allocate %ld bytes to read the RF profiles file %s", lFileSize, szFile);
      fclose(fp);
      s_bProfilesLoaded = 1;
      return 0;
   }

   size_t uRead = fread(pBuffer, 1, (size_t)lFileSize, fp);
   fclose(fp);
   if ( uRead != (size_t)lFileSize )
   {
      log_softerror_and_alarm("Failed to read the RF profiles file %s", szFile);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   u32 uStoredSize = 0;
   memcpy(&uStoredSize, pBuffer, sizeof(u32));
   if ( (u32)lFileSize != uStoredSize )
   {
      log_softerror_and_alarm("The RF profiles file %s has an invalid size (stored %u, actual %ld). Ignoring the file.",
         szFile, uStoredSize, lFileSize);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   u32 uVersion = 0;
   memcpy(&uVersion, pBuffer+sizeof(u32), sizeof(u32));
   if ( uVersion != RADIO_PROFILE_FILE_VERSION )
   {
      // Version 1 files had no CRC. They are migrated once, to version 2, and
      // are rewritten the next time the profiles are saved. Any other version
      // is not understood and the file is ignored.
      if ( uVersion == 1 )
      {
         u32 uCountV1 = 0;
         memcpy(&uCountV1, pBuffer+2*sizeof(u32), sizeof(u32));
         if ( uCountV1 > RADIO_PROFILE_MAX_COUNT )
         {
            log_softerror_and_alarm("Invalid RF profiles count in %s: %u. Ignoring the file.", szFile, uCountV1);
            free(pBuffer);
            s_bProfilesLoaded = 1;
            return 0;
         }
         u32 uExpectedV1 = (u32)(3*sizeof(u32)) + uCountV1*((u32)sizeof(radio_profile_t));
         if ( uExpectedV1 != (u32)lFileSize )
         {
            log_softerror_and_alarm("The RF profiles file %s has an unexpected size for %u profiles (%u bytes, expected %u). Ignoring the file.",
               szFile, uCountV1, (u32)lFileSize, uExpectedV1);
            free(pBuffer);
            s_bProfilesLoaded = 1;
            return 0;
         }
         const u8* pProfilesV1 = pBuffer + 3*sizeof(u32);
         for( u32 i=0; i<uCountV1; i++ )
         {
            radio_profile_t Profile;
            memcpy(&Profile, pProfilesV1 + i*sizeof(radio_profile_t), sizeof(radio_profile_t));
            // the migrated records are sanitized the same way as the version 2 ones
            _radio_profiles_sanitize(&Profile, (int)i);
            memcpy(&(s_Profiles[s_iProfilesCount]), &Profile, sizeof(radio_profile_t));
            s_iProfilesCount++;
         }
         free(pBuffer);
         s_bProfilesLoaded = 1;
         log_line("Migrated %d RF profiles from the version 1 file %s to version %u", s_iProfilesCount, szFile, RADIO_PROFILE_FILE_VERSION);
         // rewrite the file with the current format
         radio_profiles_save();
         return s_iProfilesCount;
      }
      log_softerror_and_alarm("Unsupported RF profiles file version: %u (expected %u). Ignoring the file.", uVersion, RADIO_PROFILE_FILE_VERSION);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   // the CRC is the last 4 bytes of the file, it covers everything between
   // the file size and the CRC
   u32 uCrc = 0;
   memcpy(&uCrc, pBuffer+lFileSize-sizeof(u32), sizeof(u32));
   u32 uComputedCrc = base_compute_crc32(pBuffer+sizeof(u32), (int)(lFileSize-2*sizeof(u32)));
   if ( uCrc != uComputedCrc )
   {
      log_softerror_and_alarm("The RF profiles file %s has an invalid CRC (stored %u, computed %u). Ignoring the file.",
         szFile, uCrc, uComputedCrc);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   u32 uCount = 0;
   memcpy(&uCount, pBuffer+2*sizeof(u32), sizeof(u32));
   if ( uCount > RADIO_PROFILE_MAX_COUNT )
   {
      log_softerror_and_alarm("Invalid RF profiles count in %s: %u. Ignoring the file.", szFile, uCount);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   // the file must be exactly as big as the header, the profiles and the CRC
   u32 uExpectedSize = (u32)(3*sizeof(u32)) + uCount*((u32)sizeof(radio_profile_t)) + (u32)sizeof(u32);
   if ( uExpectedSize != (u32)lFileSize )
   {
      log_softerror_and_alarm("The RF profiles file %s has an unexpected size for %u profiles (%u bytes, expected %u). Ignoring the file.",
         szFile, uCount, (u32)lFileSize, uExpectedSize);
      free(pBuffer);
      s_bProfilesLoaded = 1;
      return 0;
   }

   const u8* pProfiles = pBuffer + 3*sizeof(u32);
   for( u32 i=0; i<uCount; i++ )
   {
      radio_profile_t Profile;
      memcpy(&Profile, pProfiles + i*sizeof(radio_profile_t), sizeof(radio_profile_t));
      // sanitize the loaded values, a corrupted file must not crash the UI
      _radio_profiles_sanitize(&Profile, (int)i);
      memcpy(&(s_Profiles[s_iProfilesCount]), &Profile, sizeof(radio_profile_t));
      s_iProfilesCount++;
   }

   free(pBuffer);
   s_bProfilesLoaded = 1;
   log_line("Loaded %d RF profiles from %s", s_iProfilesCount, szFile);
   return s_iProfilesCount;
}

int radio_profiles_save()
{
   const char* szFile = radio_profiles_get_file_path();

   // make sure the folder of the profiles file exists
   char szFolder[MAX_FILE_PATH_SIZE];
   strncpy(szFolder, szFile, sizeof(szFolder)-1);
   szFolder[sizeof(szFolder)-1] = 0;
   char* pSlash = strrchr(szFolder, '/');
   if ( NULL != pSlash )
   {
      *pSlash = 0;
      if ( 0 != strlen(szFolder) )
      {
         if ( 0 != access(szFolder, F_OK) )
         {
            char szCommand[MAX_FILE_PATH_SIZE+16];
            snprintf(szCommand, sizeof(szCommand), "mkdir -p %s", szFolder);
            hw_execute_bash_command(szCommand, NULL);
         }
      }
   }

   FILE* fp = fopen(szFile, "wb");
   if ( NULL == fp )
   {
      log_softerror_and_alarm("Failed to save the RF profiles to %s", szFile);
      return 0;
   }

   u32 uHeader[3];
   uHeader[0] = 0;                                    // the file size, filled at the end
   uHeader[1] = RADIO_PROFILE_FILE_VERSION;
   uHeader[2] = s_iProfilesCount;

   // the CRC is computed over the file content, between the file size and the CRC
   u32 uContentSize = (u32)(2*sizeof(u32)) + (u32)(s_iProfilesCount*sizeof(radio_profile_t));
   u8* pContent = (u8*)malloc(uContentSize);
   if ( NULL == pContent )
   {
      log_softerror_and_alarm("Failed to allocate %u bytes to save the RF profiles", uContentSize);
      fclose(fp);
      return 0;
   }
   memcpy(pContent, &uHeader[1], 2*sizeof(u32));
   for( int i=0; i<s_iProfilesCount; i++ )
      memcpy(pContent + 2*sizeof(u32) + i*sizeof(radio_profile_t), &(s_Profiles[i]), sizeof(radio_profile_t));

   u32 uCrc = base_compute_crc32(pContent, (int)uContentSize);
   free(pContent);

   fwrite(&uHeader[0], sizeof(u32), 1, fp);
   fwrite(&uHeader[1], sizeof(u32), 1, fp);
   fwrite(&uHeader[2], sizeof(u32), 1, fp);
   for( int i=0; i<s_iProfilesCount; i++ )
      fwrite(&(s_Profiles[i]), sizeof(radio_profile_t), 1, fp);
   fwrite(&uCrc, sizeof(uCrc), 1, fp);
   fseek(fp, 0, SEEK_SET);
   u32 uFileSize = (u32)(uContentSize + 2*sizeof(u32));
   fwrite(&uFileSize, sizeof(uFileSize), 1, fp);
   fclose(fp);
   log_line("Saved %d RF profiles to %s", s_iProfilesCount, szFile);
   return 1;
}


void radio_profiles_get_default_profile(radio_profile_t* pProfile)
{
   if ( NULL == pProfile )
      return;
   memset(pProfile, 0, sizeof(radio_profile_t));
   strcpy(pProfile->szName, "New Profile");
   pProfile->iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   pProfile->iRole = RADIO_CAP_ROLE_TXRX;
   pProfile->iMode = RADIO_CAP_MODE_STATION;
   pProfile->iBand = 0;
   pProfile->uFrequencyKhz = 0;
   pProfile->iChannel = 0;
   pProfile->iBandwidthMode = RADIO_CAP_BW_20MHZ;
   pProfile->iConfiguredTxPowerDbm = DEFAULT_RADIO_TX_POWER;
   pProfile->iProfileMaxTxPowerDbm = 0;
   pProfile->iEnabled = 1;
}


int radio_profiles_resolve_interface(const radio_profile_t* pProfile)
{
   if ( NULL == pProfile )
      return -1;

   if ( pProfile->iInterfaceIndex != RADIO_PROFILE_IFACE_AUTO )
   {
      if ( (pProfile->iInterfaceIndex < 0) || (pProfile->iInterfaceIndex >= hardware_get_radio_interfaces_count()) )
         return -1;
      return pProfile->iInterfaceIndex;
   }

   // pick the first usable WiFi radio interface
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      if ( ! pRadioHWInfo->isSupported )
         continue;
      if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
         continue;
      return i;
   }
   return -1;
}


// Returns the effective frequency of a profile, resolving the automatic values.
// Returns 0 if the profile does not have a usable frequency.
static u32 _radio_profiles_get_effective_frequency(const radio_profile_t* pProfile, const radio_cap_interface_t* pCap)
{
   if ( pProfile->uFrequencyKhz != 0 )
      return pProfile->uFrequencyKhz;

   int iBand = pProfile->iBand;
   if ( iBand == 0 )
      return 0;

   const radio_cap_band_t* pBand = radio_cap_interface_get_band(pCap, iBand);
   if ( NULL == pBand )
      return 0;

   // if a channel was requested, use it
   if ( pProfile->iChannel > 0 )
   {
      for( int i=0; i<pBand->iChannelCount; i++ )
         if ( pBand->Channels[i].iChannelNumber == pProfile->iChannel )
            return pBand->Channels[i].uFrequencyKhz;
      return 0;
   }

   // otherwise use the first channel allowed by the regulatory domain
   for( int i=0; i<pBand->iChannelCount; i++ )
      if ( ! pBand->Channels[i].bDisabled )
         return pBand->Channels[i].uFrequencyKhz;

   return 0;
}


// Converts a dBm value to the raw TX power index used by the hardware.
static int _radio_profiles_dbm_to_raw(int iInterface, int iDbm)
{
   if ( iDbm <= 0 )
      return 0;

   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iInterface);
   int iCardModel = CARD_MODEL_RTL8812AU_GENERIC;
   if ( NULL != pRadioHWInfo )
      iCardModel = pRadioHWInfo->iCardModel;

   int iMw = (int)(pow(10.0, (float)iDbm/10.0) + 0.5);
   int iRaw = tx_powers_convert_mw_to_raw(hardware_getBoardType(), iCardModel, iMw);
   if ( iRaw < 1 )
      iRaw = 1;
   if ( iRaw > MAX_TX_POWER )
      iRaw = MAX_TX_POWER;
   return iRaw;
}


int radio_profiles_resolve(const radio_profile_t* pProfile, radio_profile_resolved_t* pResolved, char* szError, int iErrorSize)
{
   if ( (NULL != szError) && (iErrorSize > 0) )
      szError[0] = 0;
   if ( NULL != pResolved )
      memset(pResolved, 0, sizeof(radio_profile_resolved_t));

   if ( NULL == pProfile )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_NO_INTERFACE), iErrorSize-1);
      return RADIO_CAP_ERR_NO_INTERFACE;
   }

   if ( pProfile->szName[0] == 0 )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The profile name is empty", iErrorSize-1);
      return RADIO_CAP_ERR_INCOMPLETE;
   }

   int iInterface = radio_profiles_resolve_interface(pProfile);
   if ( iInterface < 0 )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "No usable radio interface was found", iErrorSize-1);
      return RADIO_CAP_ERR_NO_INTERFACE;
   }

   radio_cap_interface_t Cap;
   if ( ! radio_cap_probe_interface(iInterface, &Cap) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_last_error(), iErrorSize-1);
      return RADIO_CAP_ERR_NO_INTERFACE;
   }

   // The band of a profile may be left automatic, in that case the band of the
   // requested frequency is used. A frequency may also be left automatic, in
   // that case the first usable channel of the band is used.
   int iBand = pProfile->iBand;
   u32 uFrequency = pProfile->uFrequencyKhz;
   int iChannel = pProfile->iChannel;

   if ( (iBand != 0) && (uFrequency == 0) )
   {
      const radio_cap_band_t* pBand = radio_cap_interface_get_band(&Cap, iBand);
      if ( NULL == pBand )
      {
         if ( (NULL != szError) && (iErrorSize > 0) )
            strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_BAND), iErrorSize-1);
         return RADIO_CAP_ERR_BAND;
      }
      uFrequency = _radio_profiles_get_effective_frequency(pProfile, &Cap);
      if ( uFrequency == 0 )
      {
         if ( (NULL != szError) && (iErrorSize > 0) )
            strncpy(szError, "The regulatory domain does not allow any channel of the requested band", iErrorSize-1);
         return RADIO_CAP_ERR_REGULATORY;
      }
   }

   if ( (iBand == 0) && (uFrequency != 0) )
      iBand = radio_cap_get_band_index(uFrequency);

   if ( (uFrequency == 0) && (iChannel > 0) && (iBand != 0) )
   {
      const radio_cap_band_t* pBand = radio_cap_interface_get_band(&Cap, iBand);
      if ( NULL != pBand )
      {
         for( int i=0; i<pBand->iChannelCount; i++ )
            if ( pBand->Channels[i].iChannelNumber == iChannel )
            {
               uFrequency = pBand->Channels[i].uFrequencyKhz;
               break;
            }
      }
   }

   if ( uFrequency == 0 )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The profile does not have a valid frequency or channel", iErrorSize-1);
      return RADIO_CAP_ERR_INCOMPLETE;
   }

   // the channel number of the resolved frequency
   const radio_cap_channel_t* pChannel = radio_cap_interface_find_channel(&Cap, uFrequency);
   if ( (NULL != pChannel) && (iChannel == 0) )
      iChannel = pChannel->iChannelNumber;

   int iEffectiveTxPower = 0;
   int iRes = radio_cap_validate_request(&Cap, iBand, uFrequency, iChannel, pProfile->iBandwidthMode,
         pProfile->iConfiguredTxPowerDbm, pProfile->iProfileMaxTxPowerDbm,
         &iEffectiveTxPower, szError, iErrorSize);
   if ( iRes != RADIO_CAP_VALID )
      return iRes;

   if ( NULL != pResolved )
   {
      pResolved->iInterfaceIndex = iInterface;
      pResolved->iRole = pProfile->iRole;
      pResolved->iMode = pProfile->iMode;
      pResolved->iBand = iBand;
      pResolved->uFrequencyKhz = uFrequency;
      pResolved->iChannel = iChannel;
      pResolved->iBandwidthMode = pProfile->iBandwidthMode;
      pResolved->iConfiguredTxPowerDbm = pProfile->iConfiguredTxPowerDbm;
      // the requested power is the configured power, limited by the profile maximum
      pResolved->iRequestedTxPowerDbm = pProfile->iConfiguredTxPowerDbm;
      if ( (pProfile->iProfileMaxTxPowerDbm > 0) && (pResolved->iRequestedTxPowerDbm > pProfile->iProfileMaxTxPowerDbm) )
         pResolved->iRequestedTxPowerDbm = pProfile->iProfileMaxTxPowerDbm;
      // the effective power is the requested power, limited by the hardware
      pResolved->iEffectiveTxPowerDbm = iEffectiveTxPower;
      // the raw TX power index, as it is applied to the hardware
      pResolved->iRawTxPower = _radio_profiles_dbm_to_raw(iInterface, iEffectiveTxPower);
      pResolved->iEnabled = pProfile->iEnabled;
   }

   return RADIO_CAP_VALID;
}


int radio_profiles_validate(const radio_profile_t* pProfile, int* piEffectiveTxPowerDbm, char* szError, int iErrorSize)
{
   if ( NULL != piEffectiveTxPowerDbm )
      *piEffectiveTxPowerDbm = 0;

   radio_profile_resolved_t Resolved;
   int iRes = radio_profiles_resolve(pProfile, &Resolved, szError, iErrorSize);
   if ( iRes != RADIO_CAP_VALID )
      return iRes;

   if ( NULL != piEffectiveTxPowerDbm )
      *piEffectiveTxPowerDbm = Resolved.iEffectiveTxPowerDbm;

   return RADIO_CAP_VALID;
}


int radio_profiles_apply(int iIndex, char* szError, int iErrorSize)
{
   if ( (NULL != szError) && (iErrorSize > 0) )
      szError[0] = 0;

   if ( (iIndex < 0) || (iIndex >= s_iProfilesCount) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "Invalid RF profile", iErrorSize-1);
      return 0;
   }

   const radio_profile_t* pProfile = &(s_Profiles[iIndex]);
   if ( ! pProfile->iEnabled )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The RF profile is disabled", iErrorSize-1);
      return 0;
   }

   radio_profile_resolved_t Resolved;
   int iRes = radio_profiles_resolve(pProfile, &Resolved, szError, iErrorSize);
   if ( iRes != RADIO_CAP_VALID )
   {
      log_softerror_and_alarm("Cannot apply RF profile [%s]: %s", pProfile->szName,
         ((NULL != szError) && (0 != strlen(szError))) ? szError : radio_cap_get_error_description(iRes));
      return 0;
   }

   int iInterface = Resolved.iInterfaceIndex;
   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iInterface);
   if ( NULL == pRadioHWInfo )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "Invalid radio interface", iErrorSize-1);
      return 0;
   }

   if ( ! radio_profiles_configure_interface(&Resolved, szError, iErrorSize) )
      return 0;

   // 4) save the profile, so the applied values are kept
   radio_profiles_save();

   log_line("[RF-PROF] Applied RF profile [%s] to radio interface %d (%s): role %s, mode %s, frequency %u kHz, channel %d, bandwidth %d MHz, configured TX power %d dBm, requested %d dBm, effective %d dBm (raw %d)",
      pProfile->szName, Resolved.iInterfaceIndex+1,
      ((NULL != hardware_get_radio_info(Resolved.iInterfaceIndex)) ? hardware_get_radio_info(Resolved.iInterfaceIndex)->szName : "?"),
      radio_profiles_get_role_description(Resolved.iRole),
      radio_profiles_get_mode_description(Resolved.iMode),
      Resolved.uFrequencyKhz, Resolved.iChannel,
      radio_cap_bandwidth_to_mhz(Resolved.iBandwidthMode),
      Resolved.iConfiguredTxPowerDbm, Resolved.iRequestedTxPowerDbm,
      Resolved.iEffectiveTxPowerDbm, Resolved.iRawTxPower);

   return 1;
}


// Configures a radio interface with the resolved values of a profile: the
// monitor mode, the frequency, the bandwidth and the TX power. It does not
// save the profiles, it only applies the hardware configuration.
// This is used by the radio link opening path, so a profile that is bound to
// an interface is applied when the link is opened, and not only when the
// user asks for it from the user interface.
int radio_profiles_configure_interface(const radio_profile_resolved_t* pResolved, char* szError, int iErrorSize)
{
   if ( (NULL != szError) && (iErrorSize > 0) )
      szError[0] = 0;

   if ( NULL == pResolved )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "Invalid RF profile", iErrorSize-1);
      return 0;
   }

   int iInterface = pResolved->iInterfaceIndex;
   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iInterface);
   if ( NULL == pRadioHWInfo )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "Invalid radio interface", iErrorSize-1);
      return 0;
   }

   if ( ! pRadioHWInfo->isConfigurable )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The selected radio interface is not configurable", iErrorSize-1);
      return 0;
   }

   if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The RF profiles can only be applied to a WiFi radio interface", iErrorSize-1);
      return 0;
   }

   // The antenna restriction is checked before anything is changed on the
   // hardware, so a card that cannot be tuned to the requested band is not
   // touched at all. The card is not put in monitor mode and it is not tuned,
   // the profile is simply refused.
   if ( ! radio_cap_card_model_allows_frequency(pRadioHWInfo->iCardModel, pResolved->uFrequencyKhz) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_antenna_restriction_description(pRadioHWInfo->iCardModel), iErrorSize-1);
      log_softerror_and_alarm("Cannot apply the RF profile to radio interface %d (%s): %s",
         iInterface+1, pRadioHWInfo->szName,
         radio_cap_get_antenna_restriction_description(pRadioHWInfo->iCardModel));
      return 0;
   }

   // 1) the interface has to be in monitor mode to send and receive the
   //    raw Ruby packets
   radio_cap_interface_t Cap;
   memset(&Cap, 0, sizeof(radio_cap_interface_t));
   if ( ! radio_cap_probe_interface(iInterface, &Cap) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_last_error(), iErrorSize-1);
      return 0;
   }

   if ( ! Cap.bMonitorSupported )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The radio interface does not support the monitor mode, it cannot be used for RF links", iErrorSize-1);
      return 0;
   }

   int iMonitor = 0;
   int iPhy = radio_cap_probe_monitor_state(iInterface, &iMonitor);
   if ( iPhy < 0 )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_last_error(), iErrorSize-1);
      return 0;
   }
   if ( ! iMonitor )
   {
      log_line("[RF-PROF] Radio interface %d (%s) is not in monitor mode. Setting it to monitor mode.", iInterface+1, pRadioHWInfo->szName);
      if ( ! radio_cap_set_monitor_mode(iInterface, 1) )
      {
         if ( (NULL != szError) && (iErrorSize > 0) )
            strncpy(szError, radio_cap_get_last_error(), iErrorSize-1);
         return 0;
      }
   }

   // The Ruby specific monitor flags are applied after the mode change, the
   // same way the embedded platforms do it. A driver that does not accept
   // them is reported, the monitor mode stays set. The flags are applied also
   // when the interface was already in monitor mode, so a profile that is
   // applied to an interface that was set to monitor mode by other means
   // still gets the Ruby compatible flags.
   radio_cap_set_monitor_flags(iInterface);

   // 2) set the frequency and the bandwidth
   char szHTFlag[16];
   if ( 0 != radio_cap_bandwidth_to_ht_flag(pResolved->iBandwidthMode, szHTFlag, sizeof(szHTFlag)) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_BANDWIDTH), iErrorSize-1);
      return 0;
   }

   if ( ! radio_utils_set_interface_frequency_and_bandwidth(iInterface, pResolved->uFrequencyKhz, szHTFlag) )
   {
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "Failed to set the radio frequency and bandwidth", iErrorSize-1);
      return 0;
   }

   // 3) set the TX power
   int iRawTxPower = pResolved->iRawTxPower;
   if ( iRawTxPower > 0 )
   {
      int iPowerSet = 0;
      if ( hardware_radio_driver_is_rtl8812au_card(pRadioHWInfo->iRadioDriver) )
         iPowerSet = hardware_radio_set_txpower_raw_rtl8812au(iInterface, iRawTxPower);
      else if ( hardware_radio_driver_is_rtl8812eu_card(pRadioHWInfo->iRadioDriver) )
         iPowerSet = hardware_radio_set_txpower_raw_rtl8812eu(iInterface, iRawTxPower);
      else if ( hardware_radio_driver_is_rtl8733bu_card(pRadioHWInfo->iRadioDriver) )
         iPowerSet = hardware_radio_set_txpower_raw_rtl8733bu(iInterface, iRawTxPower);
      else if ( hardware_radio_driver_is_atheros_card(pRadioHWInfo->iRadioDriver) )
         iPowerSet = hardware_radio_set_txpower_raw_atheros(iInterface, iRawTxPower);
      else
      {
         log_softerror_and_alarm("No raw TX power setter for radio interface %d (%s). The TX power was not applied.",
            iInterface+1, pRadioHWInfo->szName);
         if ( (NULL != szError) && (iErrorSize > 0) )
            strncpy(szError, "This radio card has no raw TX power setter, the TX power was not applied", iErrorSize-1);
         return 0;
      }

      if ( ! iPowerSet )
      {
         log_softerror_and_alarm("Failed to apply the raw TX power %d to radio interface %d (%s).",
            iRawTxPower, iInterface+1, pRadioHWInfo->szName);
         if ( (NULL != szError) && (iErrorSize > 0) )
            strncpy(szError, "Failed to apply the TX power to the radio interface", iErrorSize-1);
         return 0;
      }
   }

   return 1;
}


// Applies the RF profile that is bound to a radio interface, if there is one.
// It is used by the radio link opening path, so a stored profile takes
// effect when the link is opened and not only when the user asks for it from
// the user interface.
// The function returns:
//    1 - a profile was found and applied
//    0 - a profile was found but it could not be applied
//   -1 - there is no enabled profile bound to this interface
int radio_profiles_apply_to_interface(int iInterfaceIndex, char* szError, int iErrorSize)
{
   if ( (NULL != szError) && (iErrorSize > 0) )
      szError[0] = 0;

   const radio_profile_t* pProfile = radio_profiles_get_by_interface(iInterfaceIndex);
   if ( NULL == pProfile )
      return -1;

   // The interface that carries the network connection of the machine is never
   // configured: an RF profile would put it in monitor mode and tune it.
   if ( hardware_radio_is_protected_interface(iInterfaceIndex) )
   {
      log_line("[RF-PROF] The RF profile [%s] is not applied to radio interface %d: it carries the network connection of this machine.",
         pProfile->szName, iInterfaceIndex+1);
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The interface carries the network connection of this machine", iErrorSize-1);
      return 0;
   }

   radio_profile_resolved_t Resolved;
   int iRes = radio_profiles_resolve(pProfile, &Resolved, szError, iErrorSize);
   if ( iRes != RADIO_CAP_VALID )
   {
      log_softerror_and_alarm("Cannot apply the RF profile [%s] to radio interface %d: %s", pProfile->szName, iInterfaceIndex+1,
         ((NULL != szError) && (0 != strlen(szError))) ? szError : radio_cap_get_error_description(iRes));
      return 0;
   }

   // The profile is bound to a specific interface, the resolved interface has
   // to be the same one.
   if ( Resolved.iInterfaceIndex != iInterfaceIndex )
   {
      log_softerror_and_alarm("The RF profile [%s] resolved to radio interface %d, but it is bound to radio interface %d. Not applying it.",
         pProfile->szName, Resolved.iInterfaceIndex+1, iInterfaceIndex+1);
      if ( (NULL != szError) && (iErrorSize > 0) )
         strncpy(szError, "The RF profile is bound to a different radio interface", iErrorSize-1);
      return 0;
   }

   if ( ! radio_profiles_configure_interface(&Resolved, szError, iErrorSize) )
      return 0;

   log_line("[RF-PROF] Applied RF profile [%s] to radio interface %d (%s): role %s, mode %s, frequency %u kHz, channel %d, bandwidth %d MHz, configured TX power %d dBm, requested %d dBm, effective %d dBm (raw %d)",
      pProfile->szName, iInterfaceIndex+1,
      ((NULL != hardware_get_radio_info(iInterfaceIndex)) ? hardware_get_radio_info(iInterfaceIndex)->szName : "?"),
      radio_profiles_get_role_description(Resolved.iRole),
      radio_profiles_get_mode_description(Resolved.iMode),
      Resolved.uFrequencyKhz, Resolved.iChannel,
      radio_cap_bandwidth_to_mhz(Resolved.iBandwidthMode),
      Resolved.iConfiguredTxPowerDbm, Resolved.iRequestedTxPowerDbm,
      Resolved.iEffectiveTxPowerDbm, Resolved.iRawTxPower);

   return 1;
}


const char* radio_profiles_get_role_description(int iRole)
{
   switch( iRole )
   {
      case RADIO_CAP_ROLE_TX: return "Transmit only";
      case RADIO_CAP_ROLE_RX: return "Receive only";
      case RADIO_CAP_ROLE_TXRX: return "Transmit and receive";
   }
   return "Unknown";
}

const char* radio_profiles_get_mode_description(int iMode)
{
   switch( iMode )
   {
      case RADIO_CAP_MODE_STATION: return "Ground station";
      case RADIO_CAP_MODE_VEHICLE: return "Vehicle";
      case RADIO_CAP_MODE_SEARCH: return "Search";
   }
   return "Unknown";
}

const char* radio_profiles_get_bandwidth_description(int iBandwidthMode)
{
   switch( iBandwidthMode )
   {
      case RADIO_CAP_BW_20MHZ: return "20 MHz";
      case RADIO_CAP_BW_40MHZ: return "40 MHz";
      case RADIO_CAP_BW_80MHZ: return "80 MHz";
   }
   return "Unknown";
}

const char* radio_profiles_get_band_description(int iBand)
{
   switch( iBand )
   {
      case RADIO_HW_SUPPORTED_BAND_23: return "2.3GHz";
      case RADIO_HW_SUPPORTED_BAND_24: return "2.4GHz";
      case RADIO_HW_SUPPORTED_BAND_25: return "2.5GHz";
      case RADIO_HW_SUPPORTED_BAND_58: return "5.8GHz";
      case RADIO_HW_SUPPORTED_BAND_433: return "433MHz";
      case RADIO_HW_SUPPORTED_BAND_868: return "868MHz";
      case RADIO_HW_SUPPORTED_BAND_915: return "915MHz";
   }
   return "Automatic";
}


// Returns the profile that is bound to a radio interface, or NULL if there is none.
//
// A profile with the automatic interface is bound to the interface that it
// resolves to, so an automatic profile is not ignored: it is the profile of
// the first usable radio interface. A profile that names an interface
// explicitly always has priority over an automatic one, because the user
// asked for that interface.
const radio_profile_t* radio_profiles_get_by_interface(int iInterfaceIndex)
{
   const radio_profile_t* pAutoProfile = NULL;

   for( int i=0; i<s_iProfilesCount; i++ )
   {
      if ( ! s_Profiles[i].iEnabled )
         continue;

      if ( s_Profiles[i].iInterfaceIndex == RADIO_PROFILE_IFACE_AUTO )
      {
         // The automatic profile is a candidate only if it resolves to this
         // interface. An explicit profile wins over it.
         if ( NULL == pAutoProfile )
         {
            if ( radio_profiles_resolve_interface(&(s_Profiles[i])) == iInterfaceIndex )
               pAutoProfile = &(s_Profiles[i]);
         }
         continue;
      }

      if ( s_Profiles[i].iInterfaceIndex != iInterfaceIndex )
         continue;
      return &(s_Profiles[i]);
   }

   return pAutoProfile;
}


int radio_profiles_get_role_for_interface(int iInterfaceIndex)
{
   const radio_profile_t* pProfile = radio_profiles_get_by_interface(iInterfaceIndex);
   if ( NULL == pProfile )
      return -1;
   return pProfile->iRole;
}


int radio_profiles_get_mode_for_interface(int iInterfaceIndex)
{
   const radio_profile_t* pProfile = radio_profiles_get_by_interface(iInterfaceIndex);
   if ( NULL == pProfile )
      return -1;
   return pProfile->iMode;
}


int radio_profiles_role_allows_tx(int iInterfaceIndex)
{
   int iRole = radio_profiles_get_role_for_interface(iInterfaceIndex);
   if ( iRole < 0 )
      return -1;
   return (iRole == RADIO_CAP_ROLE_TX) || (iRole == RADIO_CAP_ROLE_TXRX);
}


int radio_profiles_role_allows_rx(int iInterfaceIndex)
{
   int iRole = radio_profiles_get_role_for_interface(iInterfaceIndex);
   if ( iRole < 0 )
      return -1;
   return (iRole == RADIO_CAP_ROLE_RX) || (iRole == RADIO_CAP_ROLE_TXRX);
}


int radio_profiles_mode_allows_tx(int iInterfaceIndex)
{
   int iMode = radio_profiles_get_mode_for_interface(iInterfaceIndex);
   if ( iMode < 0 )
      return -1;
   // The ground station and the search mode transmit, the vehicle mode receives.
   return (iMode != RADIO_CAP_MODE_VEHICLE);
}


// Returns the index of the profile that is stored in a menu item with the given
// index. The first item of the profile list is the section title, so the
// profiles start at the menu index 1 and the profile index is the menu index
// minus one. Returns -1 if the menu index is not a profile.
int radio_profiles_index_from_menu_index(int iMenuIndex, int iProfilesCount)
{
   if ( iMenuIndex <= 0 )
      return -1;
   if ( iMenuIndex > iProfilesCount )
      return -1;
   return iMenuIndex-1;
}
