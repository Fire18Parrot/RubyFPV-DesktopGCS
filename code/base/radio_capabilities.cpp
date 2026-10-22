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
#include "config_radio.h"
#include "hardware_radio.h"
#include "hardware_procs.h"
#include "radio_capabilities.h"
#include "../common/string_utils.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static char s_szIWPATH[64] = "";
static int s_iIWCached = -1;
static char s_szLastError[512] = "";


// The `iw phy info` output is bigger than the buffer of the bash command helper,
// so the command output is read directly into a bigger buffer.


static void _radio_cap_set_error(const char* szFormat, ...)
{
   va_list args;
   va_start(args, szFormat);
   vsnprintf(s_szLastError, sizeof(s_szLastError)/sizeof(s_szLastError[0]), szFormat, args);
   va_end(args);
   s_szLastError[sizeof(s_szLastError)/sizeof(s_szLastError[0])-1] = 0;
}

const char* radio_cap_get_last_error()
{
   return s_szLastError;
}


const char* radio_cap_get_iw_path()
{
   if ( s_iIWCached < 0 )
   {
      s_iIWCached = 0;
      const char* szCandidates[] = { "/usr/sbin/iw", "/usr/bin/iw", "/sbin/iw", "/bin/iw" };
      for( int i=0; i<4; i++ )
      {
         if ( access(szCandidates[i], X_OK) == 0 )
         {
            strcpy(s_szIWPATH, szCandidates[i]);
            s_iIWCached = 1;
            break;
         }
      }
   }
   if ( s_szIWPATH[0] == 0 )
      return "";
   return s_szIWPATH;
}

int radio_cap_is_iw_available()
{
   radio_cap_get_iw_path();
   return s_iIWCached == 1;
}


int radio_cap_get_band_index(u32 uFrequencyKhz)
{
   // The band edges are the same ones used by the Ruby radio link code
   // (config_radio.c getBand()), so a frequency is classified exactly like
   // the upstream radio link code classifies it.
   if ( uFrequencyKhz < 500000 )
      return RADIO_HW_SUPPORTED_BAND_433;
   if ( uFrequencyKhz < 900000 )
      return RADIO_HW_SUPPORTED_BAND_868;
   if ( uFrequencyKhz < 950000 )
      return RADIO_HW_SUPPORTED_BAND_915;
   if ( uFrequencyKhz < 2412000 )
      return RADIO_HW_SUPPORTED_BAND_23;
   if ( uFrequencyKhz < 2487000 )
      return RADIO_HW_SUPPORTED_BAND_24;
   if ( uFrequencyKhz < 5000000 )
      return RADIO_HW_SUPPORTED_BAND_25;
   if ( uFrequencyKhz <= 5895000 )
      return RADIO_HW_SUPPORTED_BAND_58;
   return 0;
}

int radio_cap_is_frequency_in_band(u32 uFrequencyKhz)
{
   return radio_cap_get_band_index(uFrequencyKhz) != 0;
}


int radio_cap_bandwidth_to_mhz(int iBandwidthMode)
{
   switch( iBandwidthMode )
   {
      case RADIO_CAP_BW_20MHZ: return 20;
      case RADIO_CAP_BW_40MHZ: return 40;
      case RADIO_CAP_BW_80MHZ: return 80;
   }
   return 0;
}

int radio_cap_bandwidth_to_ht_flag(int iBandwidthMode, char* szOut, int iOutSize)
{
   if ( (NULL == szOut) || (iOutSize < 8) )
      return -1;
   switch( iBandwidthMode )
   {
      case RADIO_CAP_BW_20MHZ: strcpy(szOut, "HT20"); return 0;
      case RADIO_CAP_BW_40MHZ: strcpy(szOut, "HT40+"); return 0;
      case RADIO_CAP_BW_80MHZ: strcpy(szOut, "80MHz"); return 0;
   }
   strcpy(szOut, "HT20");
   return -1;
}


// Parses a line of the form:
//   * 2412.0 MHz [1] (20.0 dBm)
//   * 5745.0 MHz [149] (13.0 dBm)
//   * 2484.0 MHz [14] (disabled)
// Returns 1 if the line was a channel line.
static int _radio_cap_parse_channel_line(const char* szLine, radio_cap_channel_t* pChannel)
{
   if ( NULL == szLine )
      return 0;

   // find the MHz value
   const char* p = strstr(szLine, "MHz");
   if ( NULL == p )
      return 0;

   // walk back over the spaces and then over the number
   const char* pEnd = p;
   while ( (pEnd > szLine) && (' ' == *(pEnd-1)) )
      pEnd--;
   while ( (pEnd > szLine) && (isdigit((unsigned char)*(pEnd-1)) || (*(pEnd-1) == '.') ) )
      pEnd--;
   if ( pEnd == p )
      return 0;

   double dMHz = atof(pEnd);
   if ( (dMHz < 100.0) || (dMHz > 20000.0) )
      return 0;

   pChannel->uFrequencyKhz = (u32)(dMHz * 1000.0);

   // channel number
   pChannel->iChannelNumber = 0;
   const char* pB = strchr(szLine, '[');
   if ( NULL != pB )
   {
      pB++;
      pChannel->iChannelNumber = atoi(pB);
   }

   // max tx power, in dBm
   pChannel->iMaxTxPowerDbm = -1;
   const char* pD = strstr(szLine, "dBm");
   if ( NULL != pD )
   {
      // the number is right before the "dBm" token, ex: "(20.0 dBm)"
      const char* pDEnd = pD;
      while ( (pDEnd > szLine) && (isspace((unsigned char)*(pDEnd-1))) )
         pDEnd--;
      const char* pDStart = pDEnd;
      while ( (pDStart > szLine) && (isdigit((unsigned char)*(pDStart-1)) || (*(pDStart-1) == '.') ) )
         pDStart--;
      if ( pDStart != pDEnd )
      {
         double dDbm = atof(pDStart);
         if ( (dDbm > -50.0) && (dDbm < 100.0) )
            pChannel->iMaxTxPowerDbm = (int)(dDbm + 0.5);
      }
   }

   pChannel->bDisabled = 0;
   if ( NULL != strstr(szLine, "disabled") )
      pChannel->bDisabled = 1;
   if ( NULL != strstr(szLine, "no IR") )
      pChannel->bDisabled = 1;

   pChannel->bRadarDetection = ( NULL != strstr(szLine, "radar detection") ) ? 1 : 0;

   pChannel->iBandIndex = radio_cap_get_band_index(pChannel->uFrequencyKhz);

   return 1;
}


// Finds the band of an interface, creating it if it does not exist yet.
// Returns NULL if the maximum number of bands was reached.
static radio_cap_band_t* _radio_cap_get_or_create_band(radio_cap_interface_t* pOut, int iBandIndex)
{
   for( int i=0; i<pOut->iBandCount; i++ )
      if ( pOut->Bands[i].iBandIndex == iBandIndex )
         return &(pOut->Bands[i]);

   if ( pOut->iBandCount >= RADIO_CAP_MAX_BANDS )
      return NULL;

   radio_cap_band_t* pBand = &(pOut->Bands[pOut->iBandCount]);
   memset(pBand, 0, sizeof(radio_cap_band_t));
   pBand->iBandIndex = iBandIndex;
   pBand->iMaxTxPowerDbm = -1;
   // 20MHz is the mandatory minimum for any band that has channels
   pBand->bSupports20MHz = 1;
   pOut->iBandCount++;
   return pBand;
}


// Parses the output of `iw phy<N> info`.
//
// The output has one section per band:
//   Band 1:
//       Capabilities: 0x196e
//           HT20/HT40
//       VHT Capabilities (0x03d071a2):
//           Supported Channel Width: neither 160 nor 80+80
//           short GI (80 MHz)
//       Frequencies:
//           * 2412.0 MHz [1] (20.0 dBm)
//
// The HT and VHT capabilities are reported before the channel list, and the
// band index of a section is only known from its channels. The capabilities
// are therefore collected per section and applied to the band once the
// channels of that section were identified.
int radio_cap_parse_iw_phy_info(const char* szInfo, radio_cap_interface_t* pOut)
{
   if ( (NULL == szInfo) || (NULL == pOut) )
      return -1;

   memset(pOut, 0, sizeof(radio_cap_interface_t));
   pOut->iPhyIndex = -1;
   pOut->iMaxTxPowerDbm = -1;

   int bHasMonitor = 0;
   int bInModes = 0;

   // The capabilities of the band section being parsed
   int bSectionHasHT40 = 0;
   int bSectionHasVHT80 = 0;
   int bInFrequencies = 0;
   int iCount = 0;

   // The bands of the sections, in the order they were found. The section
   // capabilities are applied to these bands at the end of the section.
   int iSectionBands[RADIO_CAP_MAX_BANDS];
   int iSectionBandCount = 0;
   for( int i=0; i<RADIO_CAP_MAX_BANDS; i++ )
      iSectionBands[i] = 0;

   const char* p = szInfo;
   while( NULL != p )
   {
      const char* pEol = strchr(p, '\n');
      size_t len = ( NULL != pEol ) ? (size_t)(pEol - p) : strlen(p);
      char szLine[512];
      if ( len >= sizeof(szLine) )
         len = sizeof(szLine)-1;
      memcpy(szLine, p, len);
      szLine[len] = 0;

      // --- phy index ---
      if ( NULL != strstr(szLine, "wiphy index:") )
      {
         const char* pVal = strstr(szLine, "wiphy index:");
         if ( NULL != pVal )
         {
            pVal += strlen("wiphy index:");
            while ( (' ' == *pVal) || ('\t' == *pVal) )
               pVal++;
            if ( ('0' <= *pVal) && ('9' >= *pVal) )
               pOut->iPhyIndex = atoi(pVal);
         }
      }

      // --- interface modes ---
      if ( NULL != strstr(szLine, "Supported interface modes") )
         bInModes = 1;
      else if ( bInModes && (NULL != strstr(szLine, "Band ")) )
         bInModes = 0;

      if ( bInModes && (NULL != strstr(szLine, "monitor")) )
         bHasMonitor = 1;

      // --- band sections ---
      if ( NULL != strstr(szLine, "Band ") )
      {
         // apply the capabilities of the previous section to its bands
         for( int i=0; i<iSectionBandCount; i++ )
         {
            radio_cap_band_t* pBand = NULL;
            for( int k=0; k<pOut->iBandCount; k++ )
               if ( pOut->Bands[k].iBandIndex == iSectionBands[i] )
                  pBand = &(pOut->Bands[k]);
            if ( NULL == pBand )
               continue;
            if ( bSectionHasHT40 )
               pBand->bSupports40MHz = 1;
            if ( bSectionHasVHT80 )
               pBand->bSupports80MHz = 1;
         }

         bSectionHasHT40 = 0;
         bSectionHasVHT80 = 0;
         bInFrequencies = 0;
         iSectionBandCount = 0;
      }
      else if ( NULL != strstr(szLine, "Frequencies:") )
      {
         bInFrequencies = 1;
      }
      else if ( (0 == bInFrequencies) && (NULL != strstr(szLine, "HT20/HT40")) )
      {
         bSectionHasHT40 = 1;
      }
      else if ( (0 == bInFrequencies) && (NULL != strstr(szLine, "short GI (80 MHz)")) )
      {
         bSectionHasVHT80 = 1;
      }
      else if ( (0 == bInFrequencies) && (NULL != strstr(szLine, "Supported Channel Width")) )
      {
         if ( (NULL != strstr(szLine, "80 MHz")) || (NULL != strstr(szLine, "80MHz")) )
            bSectionHasVHT80 = 1;
      }

      // --- channels ---
      if ( bInFrequencies )
      {
         radio_cap_channel_t Channel;
         memset(&Channel, 0, sizeof(radio_cap_channel_t));
         if ( _radio_cap_parse_channel_line(szLine, &Channel) && (Channel.iBandIndex != 0) )
         {
            radio_cap_band_t* pBand = _radio_cap_get_or_create_band(pOut, Channel.iBandIndex);
            if ( NULL != pBand )
            {
               if ( pBand->iChannelCount >= RADIO_CAP_MAX_CHANNELS )
               {
                  // no room left in this band
               }
               else
               {
                  Channel.iBandIndex = pBand->iBandIndex;
                  pBand->Channels[pBand->iChannelCount] = Channel;
                  pBand->iChannelCount++;
                  iCount++;

                  if ( (Channel.iMaxTxPowerDbm > 0) && ((pBand->iMaxTxPowerDbm < 0) || (Channel.iMaxTxPowerDbm > pBand->iMaxTxPowerDbm)) )
                     pBand->iMaxTxPowerDbm = Channel.iMaxTxPowerDbm;
                  if ( (Channel.iMaxTxPowerDbm > 0) && ((pOut->iMaxTxPowerDbm < 0) || (Channel.iMaxTxPowerDbm > pOut->iMaxTxPowerDbm)) )
                     pOut->iMaxTxPowerDbm = Channel.iMaxTxPowerDbm;

                  // remember the band of this section
                  bool bFound = false;
                  for( int i=0; i<iSectionBandCount; i++ )
                     if ( iSectionBands[i] == pBand->iBandIndex )
                        bFound = true;
                  if ( ! bFound && (iSectionBandCount < RADIO_CAP_MAX_BANDS) )
                  {
                     iSectionBands[iSectionBandCount] = pBand->iBandIndex;
                     iSectionBandCount++;
                  }
               }
            }
         }
      }

      if ( NULL == pEol )
         break;
      p = pEol + 1;
   }

   // apply the capabilities of the last section to its bands
   for( int i=0; i<iSectionBandCount; i++ )
   {
      radio_cap_band_t* pBand = NULL;
      for( int k=0; k<pOut->iBandCount; k++ )
         if ( pOut->Bands[k].iBandIndex == iSectionBands[i] )
            pBand = &(pOut->Bands[k]);
      if ( NULL == pBand )
         continue;
      if ( bSectionHasHT40 )
         pBand->bSupports40MHz = 1;
      if ( bSectionHasVHT80 )
         pBand->bSupports80MHz = 1;
   }

   pOut->bMonitorSupported = bHasMonitor;

   return iCount;
}


// Parses the output of `iw dev` to find the phy of an interface.
//
// The real output of `iw dev` does not have a "wiphy N" line. The phy index
// is in the "phy#N" section header, and the header applies to all the
// interfaces that follow it, until the next header:
//
//   phy#2
//   	Interface wlan1
//   		ifindex 6
//   		wdev 0x200000001
//   		addr 26:62:c3:3c:6e:b9
//   		type managed
//   		txpower 23.00 dBm
//   phy#1
//   	Unnamed/non-netdev interface
//   	Interface wlan0
//   		type managed
//   		channel 6 (2437 MHz), width: 40 MHz, center1: 2447 MHz
//
// So the phy index of an interface is the index of the last "phy#N" header
// that was read before the interface block. The "wiphy N" line is still
// accepted, for the outputs that have it.
int radio_cap_parse_iw_dev_info(const char* szInfo, const char* szInterfaceName, int* piMonitorEnabled)
{
   if ( (NULL == szInfo) || (NULL == szInterfaceName) )
      return -1;

   if ( NULL != piMonitorEnabled )
      *piMonitorEnabled = 0;

   int bFound = 0;
   int iMonitor = 0;
   int iPhy = -1;
   int iCurrentPhy = -1;
   int bInInterface = 0;

   const char* p = szInfo;
   while( NULL != p )
   {
      const char* pEol = strchr(p, '\n');
      size_t len = ( NULL != pEol ) ? (size_t)(pEol - p) : strlen(p);
      char szLine[512];
      if ( len >= sizeof(szLine) )
         len = sizeof(szLine)-1;
      memcpy(szLine, p, len);
      szLine[len] = 0;

      // the interface blocks are indented with a tab
      const char* pTrim = szLine;
      while ( (' ' == *pTrim) || ('\t' == *pTrim) )
         pTrim++;

      if ( 0 == strncmp(pTrim, "phy#", 4) )
      {
         // a new phy section starts here, it applies to everything that
         // follows it, until the next section
         iCurrentPhy = atoi(pTrim + 4);
         bInInterface = 0;
      }
      else if ( 0 == strncmp(pTrim, "Interface ", 10) )
      {
         const char* pName = pTrim + 10;
         while ( (' ' == *pName) && (*pName != 0) )
            pName++;

         size_t iNameLen = strlen(szInterfaceName);
         if ( (0 == strncmp(pName, szInterfaceName, iNameLen)) &&
              ((0 == pName[iNameLen]) || (' ' == pName[iNameLen]) || ('\t' == pName[iNameLen])) )
         {
            bFound = 1;
            bInInterface = 1;
            // the phy index is the one of the section the interface is in
            iPhy = iCurrentPhy;
         }
         else
            bInInterface = 0;
      }
      else if ( bInInterface && (0 == strncmp(pTrim, "type ", 5)) )
      {
         if ( NULL != strstr(pTrim, "monitor") )
            iMonitor = 1;
      }
      else if ( bInInterface && (0 == strncmp(pTrim, "wiphy ", 6)) )
      {
         iPhy = atoi(pTrim + 6);
      }

      if ( NULL == pEol )
         break;
      p = pEol + 1;
   }

   if ( NULL != piMonitorEnabled )
      *piMonitorEnabled = iMonitor;

   if ( ! bFound )
      return -1;
   return iPhy;
}


int radio_cap_interface_band_count(const radio_cap_interface_t* pCap)
{
   if ( NULL == pCap )
      return 0;
   return pCap->iBandCount;
}

const radio_cap_band_t* radio_cap_interface_get_band(const radio_cap_interface_t* pCap, int iBandIndex)
{
   if ( NULL == pCap )
      return NULL;
   for( int i=0; i<pCap->iBandCount; i++ )
      if ( pCap->Bands[i].iBandIndex == iBandIndex )
         return &(pCap->Bands[i]);
   return NULL;
}


u32 radio_cap_interface_get_supported_bands_mask(const radio_cap_interface_t* pCap)
{
   u32 uMask = 0;
   if ( NULL == pCap )
      return 0;

   // The band index of the capability model is the value of the
   // RADIO_HW_SUPPORTED_BAND_* constant, it is already a bit mask value and
   // it must not be shifted again. Shifting it would map the 2.4 GHz band
   // to the 2.5 GHz band and the 5.8 GHz band to a value that is not a band
   // at all.
   for( int i=0; i<pCap->iBandCount; i++ )
   {
      int iBand = pCap->Bands[i].iBandIndex;
      if ( (iBand < 1) || (iBand > 0x10000) )
         continue;
      uMask |= (u32)iBand;
   }
   return uMask;
}

int radio_cap_interface_supports_band(const radio_cap_interface_t* pCap, int iBand)
{
   return NULL != radio_cap_interface_get_band(pCap, iBand);
}

const radio_cap_channel_t* radio_cap_interface_find_channel(const radio_cap_interface_t* pCap, u32 uFrequencyKhz)
{
   if ( NULL == pCap )
      return NULL;
   for( int i=0; i<pCap->iBandCount; i++ )
   {
      const radio_cap_band_t* pBand = &(pCap->Bands[i]);
      for( int k=0; k<pBand->iChannelCount; k++ )
         if ( pBand->Channels[k].uFrequencyKhz == uFrequencyKhz )
            return &(pBand->Channels[k]);
   }
   return NULL;
}

int radio_cap_interface_supports_frequency(const radio_cap_interface_t* pCap, u32 uFrequencyKhz)
{
   return NULL != radio_cap_interface_find_channel(pCap, uFrequencyKhz);
}

int radio_cap_interface_get_max_txpower_dbm(const radio_cap_interface_t* pCap, u32 uFrequencyKhz)
{
   const radio_cap_channel_t* pChannel = radio_cap_interface_find_channel(pCap, uFrequencyKhz);
   if ( NULL == pChannel )
      return -1;
   return pChannel->iMaxTxPowerDbm;
}

int radio_cap_interface_get_band_max_txpower_dbm(const radio_cap_interface_t* pCap, int iBand)
{
   const radio_cap_band_t* pBand = radio_cap_interface_get_band(pCap, iBand);
   if ( NULL == pBand )
      return -1;
   return pBand->iMaxTxPowerDbm;
}

int radio_cap_interface_supports_bandwidth(const radio_cap_interface_t* pCap, int iBand, int iBandwidthMode)
{
   if ( (NULL == pCap) || (iBandwidthMode < 0) || (iBandwidthMode >= RADIO_CAP_BW_COUNT) )
      return 0;

   const radio_cap_band_t* pBand = radio_cap_interface_get_band(pCap, iBand);
   if ( NULL == pBand )
      return 0;

   // The bandwidth is only supported if the phy advertises it for this band.
   if ( RADIO_CAP_BW_20MHZ == iBandwidthMode )
      return pBand->bSupports20MHz ? 1 : 0;
   if ( RADIO_CAP_BW_40MHZ == iBandwidthMode )
      return pBand->bSupports40MHz ? 1 : 0;
   if ( RADIO_CAP_BW_80MHZ == iBandwidthMode )
      return pBand->bSupports80MHz ? 1 : 0;
   return 0;
}

const char* radio_cap_interface_get_bands_description(const radio_cap_interface_t* pCap)
{
   static char s_szDescription[512];
   s_szDescription[0] = 0;
   if ( NULL == pCap )
      return s_szDescription;

   char szBuff[64];
   for( int i=0; i<pCap->iBandCount; i++ )
   {
      const radio_cap_band_t* pBand = &(pCap->Bands[i]);
      if ( 0 != strlen(s_szDescription) )
         strcat(s_szDescription, ", ");
      switch( pBand->iBandIndex )
      {
         case RADIO_HW_SUPPORTED_BAND_23: strcat(s_szDescription, "2.3GHz"); break;
         case RADIO_HW_SUPPORTED_BAND_24: strcat(s_szDescription, "2.4GHz"); break;
         case RADIO_HW_SUPPORTED_BAND_25: strcat(s_szDescription, "2.5GHz"); break;
         case RADIO_HW_SUPPORTED_BAND_58: strcat(s_szDescription, "5.8GHz"); break;
         case RADIO_HW_SUPPORTED_BAND_433: strcat(s_szDescription, "433MHz"); break;
         case RADIO_HW_SUPPORTED_BAND_868: strcat(s_szDescription, "868MHz"); break;
         case RADIO_HW_SUPPORTED_BAND_915: strcat(s_szDescription, "915MHz"); break;
         default: break;
      }
      if ( pBand->iMaxTxPowerDbm > 0 )
      {
         snprintf(szBuff, sizeof(szBuff)/sizeof(szBuff[0]), " (max %d dBm)", pBand->iMaxTxPowerDbm);
         strcat(s_szDescription, szBuff);
      }
   }
   return s_szDescription;
}

const char* radio_cap_interface_get_bandwidths_description(const radio_cap_interface_t* pCap, int iBand)
{
   static char s_szDescription[128];
   s_szDescription[0] = 0;
   const radio_cap_band_t* pBand = radio_cap_interface_get_band(pCap, iBand);
   if ( NULL == pBand )
      return s_szDescription;

   char szBuff[32];
   if ( pBand->bSupports20MHz )
      strcat(s_szDescription, "20MHz");
   if ( pBand->bSupports40MHz )
   {
      if ( 0 != strlen(s_szDescription) )
         strcat(s_szDescription, ", ");
      strcat(s_szDescription, "40MHz");
   }
   if ( pBand->bSupports80MHz )
   {
      if ( 0 != strlen(s_szDescription) )
         strcat(s_szDescription, ", ");
      strcat(s_szDescription, "80MHz");
   }
   (void)szBuff;
   return s_szDescription;
}


const char* radio_cap_get_error_description(int iErrorCode)
{
   switch( iErrorCode )
   {
      case RADIO_CAP_VALID: return "OK";
      case RADIO_CAP_ERR_NO_INTERFACE: return "No radio interface selected";
      case RADIO_CAP_ERR_UNSUPPORTED: return "The selected radio interface does not support this configuration";
      case RADIO_CAP_ERR_BAND: return "The selected radio interface does not support the requested band";
      case RADIO_CAP_ERR_FREQUENCY: return "The requested frequency is not available on the selected radio interface";
      case RADIO_CAP_ERR_CHANNEL: return "The requested channel is not available on the selected radio interface";
      case RADIO_CAP_ERR_BANDWIDTH: return "The requested bandwidth is not supported";
      case RADIO_CAP_ERR_TXPOWER: return "The requested TX power is not supported";
      case RADIO_CAP_ERR_REGULATORY: return "The requested TX power exceeds the regulatory limit";
      case RADIO_CAP_ERR_INCOMPLETE: return "The configuration is incomplete";
      case RADIO_CAP_ERR_NOT_APPLIED: return "The configuration was not applied";
      case RADIO_CAP_ERR_NO_IW: return "The 'iw' tool is not installed";
      case RADIO_CAP_ERR_NO_MONITOR: return "The radio interface does not support monitor mode";
      case RADIO_CAP_ERR_NO_PERMISSION: return "Not enough permissions to control the radio interface";
      case RADIO_CAP_ERR_ANTENNA: return "The radio card has 5 GHz only antennas, the 2.4 GHz band cannot be used";
   }
   return "Unknown error";
}


// --- Antenna restriction (hardware safety) ---
//
// Some cards are fitted with 5 GHz-only antennas. Tuning such a card to the
// 2.4 GHz band can physically destroy the card, so the 2.4 GHz band has to be
// refused before any frequency is applied to it.
//
// The restriction is expressed in the card model, because the antennas are a
// property of the card and not of the driver or of the radio link. A card
// model that is not in the list of restricted models is NOT restricted: an
// unrecognised card must never be blocked by mistake, because a wrong block
// would make a perfectly good card unusable.

int radio_cap_card_model_is_5ghz_only(int iCardModel)
{
   // A negative card model is the same card model, the sign is only used
   // elsewhere to mark a card that has to be ignored.
   if ( iCardModel < 0 )
      iCardModel = -iCardModel;

   // The RTL8812AU DualAnt card is fitted with 5 GHz-only antennas.
   if ( iCardModel == CARD_MODEL_RTL8812AU_DUAL_ANTENNA )
      return 1;

   return 0;
}


int radio_cap_card_model_allows_frequency(int iCardModel, u32 uFrequencyKhz)
{
   if ( ! radio_cap_card_model_is_5ghz_only(iCardModel) )
      return 1;

   // The 2.4 GHz band is the band that the 5 GHz-only antennas cannot handle.
   // The band edges are the same ones used by radio_cap_get_band_index(), so
   // a frequency is classified exactly like the rest of the code classifies
   // it.
   int iBand = radio_cap_get_band_index(uFrequencyKhz);
   if ( (iBand == RADIO_HW_SUPPORTED_BAND_23) ||
        (iBand == RADIO_HW_SUPPORTED_BAND_24) ||
        (iBand == RADIO_HW_SUPPORTED_BAND_25) )
      return 0;

   return 1;
}


const char* radio_cap_get_antenna_restriction_description(int iCardModel)
{
   static char s_szDescription[256];
   s_szDescription[0] = 0;

   if ( ! radio_cap_card_model_is_5ghz_only(iCardModel) )
      return s_szDescription;

   snprintf(s_szDescription, sizeof(s_szDescription)/sizeof(s_szDescription[0]),
      "The %s card has 5 GHz-only antennas and it cannot be used on the 2.4 GHz band. "
      "Tuning it to 2.4 GHz would damage the card. Use a 2.4+5.8 GHz adapter for the 2.4 GHz band.",
      str_get_radio_card_model_string(iCardModel));

   return s_szDescription;
}


int radio_cap_validate_request(const radio_cap_interface_t* pCap,
      int iBand, u32 uFrequencyKhz, int iChannel, int iBandwidthMode,
      int iRequestedTxPowerDbm, int iProfileMaxTxPowerDbm,
      int* piEffectiveTxPowerDbm, char* szError, int iErrorSize)
{
   if ( NULL != szError && iErrorSize > 0 )
      szError[0] = 0;
   if ( NULL != piEffectiveTxPowerDbm )
      *piEffectiveTxPowerDbm = 0;

   if ( NULL == pCap )
   {
      if ( NULL != szError && iErrorSize > 0 )
         strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_NO_INTERFACE), iErrorSize-1);
      return RADIO_CAP_ERR_NO_INTERFACE;
   }

   // Band check
   if ( iBand != 0 )
   {
      const radio_cap_band_t* pBand = radio_cap_interface_get_band(pCap, iBand);
      if ( NULL == pBand )
      {
         if ( NULL != szError && iErrorSize > 0 )
            strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_BAND), iErrorSize-1);
         return RADIO_CAP_ERR_BAND;
      }
   }

   // Frequency check
   const radio_cap_channel_t* pChannel = NULL;
   if ( uFrequencyKhz != 0 )
   {
      pChannel = radio_cap_interface_find_channel(pCap, uFrequencyKhz);
      if ( NULL == pChannel )
      {
         if ( NULL != szError && iErrorSize > 0 )
            strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_FREQUENCY), iErrorSize-1);
         return RADIO_CAP_ERR_FREQUENCY;
      }
      if ( pChannel->bDisabled )
      {
         if ( NULL != szError && iErrorSize > 0 )
            strncpy(szError, "The requested channel is disabled by the regulatory domain", iErrorSize-1);
         return RADIO_CAP_ERR_REGULATORY;
      }
      // Channel number check, if one was requested
      if ( (iChannel > 0) && (pChannel->iChannelNumber > 0) && (pChannel->iChannelNumber != iChannel) )
      {
         if ( NULL != szError && iErrorSize > 0 )
            strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_CHANNEL), iErrorSize-1);
         return RADIO_CAP_ERR_CHANNEL;
      }
   }

   // Bandwidth check
   {
      if ( (iBandwidthMode < 0) || (iBandwidthMode >= RADIO_CAP_BW_COUNT) )
      {
         if ( NULL != szError && iErrorSize > 0 )
            strncpy(szError, "Invalid bandwidth mode", iErrorSize-1);
         return RADIO_CAP_ERR_BANDWIDTH;
      }
      int iBandForCheck = iBand;
      if ( iBandForCheck == 0 )
         iBandForCheck = radio_cap_get_band_index(uFrequencyKhz);
      if ( iBandForCheck != 0 )
      {
         if ( ! radio_cap_interface_supports_bandwidth(pCap, iBandForCheck, iBandwidthMode) )
         {
            if ( NULL != szError && iErrorSize > 0 )
            {
               snprintf(szError, iErrorSize, "The requested bandwidth (%d MHz) is not supported by the radio interface. Supported: %s",
                  radio_cap_bandwidth_to_mhz(iBandwidthMode), radio_cap_interface_get_bandwidths_description(pCap, iBandForCheck));
            }
            return RADIO_CAP_ERR_BANDWIDTH;
         }
      }
   }

   // TX power: the effective power is the minimum of:
   //   - the requested power
   //   - the profile maximum power
   //   - the regulatory/hardware maximum reported for the channel
   // The per channel limit reported by the wireless stack is already the
   // regulatory limit of that channel, so no global ceiling is applied on top.
   int iEffective = iRequestedTxPowerDbm;
   int iHardwareMax = -1;
   if ( NULL != pChannel )
      iHardwareMax = pChannel->iMaxTxPowerDbm;
   if ( (iHardwareMax < 0) && (iBand != 0) )
      iHardwareMax = radio_cap_interface_get_band_max_txpower_dbm(pCap, iBand);
   if ( (iHardwareMax < 0) && (pCap->iMaxTxPowerDbm > 0) )
      iHardwareMax = pCap->iMaxTxPowerDbm;

   if ( iRequestedTxPowerDbm < 0 )
   {
      if ( NULL != szError && iErrorSize > 0 )
         strncpy(szError, radio_cap_get_error_description(RADIO_CAP_ERR_TXPOWER), iErrorSize-1);
      return RADIO_CAP_ERR_TXPOWER;
   }

   if ( (iProfileMaxTxPowerDbm > 0) && (iEffective > iProfileMaxTxPowerDbm) )
      iEffective = iProfileMaxTxPowerDbm;

   if ( (iHardwareMax > 0) && (iEffective > iHardwareMax) )
      iEffective = iHardwareMax;

   if ( iEffective < 0 )
      iEffective = 0;

   if ( NULL != piEffectiveTxPowerDbm )
      *piEffectiveTxPowerDbm = iEffective;

   return RADIO_CAP_VALID;
}


// Runs a command and captures its output into a big enough buffer.
// Returns 1 if the command ran and exited with the status 0, 0 if the
// command could not be started or if it exited with a non zero status.
// The exit status of the command is stored in piExitStatus, when it is not
// NULL.
static int _radio_cap_run_command(const char* szCommand, char* pOutput, int iOutputSize, int* piExitStatus)
{
   if ( NULL != piExitStatus )
      *piExitStatus = -1;

   if ( (NULL == szCommand) || (NULL == pOutput) || (iOutputSize < 64) )
      return 0;

   pOutput[0] = 0;
   FILE* fp = popen(szCommand, "r");
   if ( NULL == fp )
   {
      _radio_cap_set_error("Failed to execute the command: %s", szCommand);
      return 0;
   }

   int iCount = 0;
   while( ! feof(fp) && ! ferror(fp) )
   {
      if ( iCount >= iOutputSize - 2 )
         break;
      size_t iRead = fread(pOutput + iCount, 1, iOutputSize - 2 - iCount, fp);
      if ( 0 == iRead )
         break;
      iCount += (int)iRead;
   }
   pOutput[iCount] = 0;

   int iRes = pclose(fp);
   if ( -1 == iRes )
   {
      // the command could not be closed, the exit status is not known
      _radio_cap_set_error("Failed to run the command '%s': %s", szCommand,
         (0 != strlen(pOutput)) ? pOutput : "unknown error");
      return 0;
   }

   // pclose returns the wait status of the command, the exit status is in
   // the higher bits of it.
   int iExitStatus = (iRes & 0xFF00) >> 8;
   if ( NULL != piExitStatus )
      *piExitStatus = iExitStatus;

   if ( 0 != iExitStatus )
   {
      // the command failed, the output contains the error message
      _radio_cap_set_error("The command '%s' failed with the exit status %d: %s", szCommand,
         iExitStatus, (0 != strlen(pOutput)) ? pOutput : "unknown error");
      return 0;
   }
   return 1;
}


// Decides if a radio control command succeeded.
//
// The decision is based on the result of the command runner and on the exit
// status of the command. The output of the command is searched only for the
// known error messages, to catch a command that reports an error in its
// output but exits with the status 0.
//
// A command that could not be started (0) or that timed out (-1) is a
// failure, and so is a command that exited with a non zero status.
int radio_cap_command_succeeded(int iCommandResult, int iExitStatus, const char* szOutput)
{
   // The command did not run, or it timed out.
   if ( 1 != iCommandResult )
      return 0;

   // The command ran and it failed.
   if ( 0 != iExitStatus )
      return 0;

   if ( NULL == szOutput )
      return 1;

   // The known error messages of the radio control commands. A command that
   // reports one of them did not do what it was asked to do.
   if ( (NULL != strstr(szOutput, "command failed")) ||
        (NULL != strstr(szOutput, "Operation not permitted")) ||
        (NULL != strstr(szOutput, "No such device")) ||
        (NULL != strstr(szOutput, "not found")) ||
        (NULL != strstr(szOutput, "invalid argument")) ||
        (NULL != strstr(szOutput, "Usage:")) ||
        (NULL != strstr(szOutput, "Set failed")) )
      return 0;

   return 1;
}


int radio_cap_probe_interface(int iRadioIndex, radio_cap_interface_t* pOut)
{
   if ( (NULL == pOut) || (iRadioIndex < 0) )
   {
      _radio_cap_set_error("Invalid radio interface index");
      return 0;
   }

   s_szLastError[0] = 0;

   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iRadioIndex);
   if ( NULL == pRadioHWInfo )
   {
      _radio_cap_set_error("Invalid radio interface %d", iRadioIndex+1);
      return 0;
   }

   if ( ! radio_cap_is_iw_available() )
   {
      _radio_cap_set_error("The 'iw' tool is not installed. Install the 'iw' package to use the WiFi radios.");
      log_softerror_and_alarm("Cannot probe radio interface %d: the 'iw' tool is not installed.", iRadioIndex+1);
      return 0;
   }

   memset(pOut, 0, sizeof(radio_cap_interface_t));
   strncpy(pOut->szName, pRadioHWInfo->szName, sizeof(pOut->szName)/sizeof(pOut->szName[0])-1);
   pOut->iPhyIndex = pRadioHWInfo->phy_index;

   char* szOutput = (char*)malloc(RADIO_CAP_IW_OUTPUT_SIZE);
   if ( NULL == szOutput )
   {
      _radio_cap_set_error("Out of memory");
      return 0;
   }

   char szComm[256];

   // interface state (monitor mode) and phy index
   snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), "%s dev 2>&1", radio_cap_get_iw_path());
   if ( ! _radio_cap_run_command(szComm, szOutput, RADIO_CAP_IW_OUTPUT_SIZE, NULL) )
   {
      // not fatal, the phy index can still come from the radio info
      szOutput[0] = 0;
   }
   int iMonitor = 0;
   int iPhy = radio_cap_parse_iw_dev_info(szOutput, pRadioHWInfo->szName, &iMonitor);
   if ( iPhy >= 0 )
      pOut->iPhyIndex = iPhy;
   pOut->bMonitorEnabled = iMonitor;

   // phy capabilities
   if ( pOut->iPhyIndex < 0 )
   {
      _radio_cap_set_error("Failed to determine the phy index of radio interface %d (%s)", iRadioIndex+1, pRadioHWInfo->szName);
      log_softerror_and_alarm("Failed to determine the phy index of radio interface %d (%s).", iRadioIndex+1, pRadioHWInfo->szName);
      free(szOutput);
      return 0;
   }

   snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), "%s phy%d info 2>&1", radio_cap_get_iw_path(), pOut->iPhyIndex);
   if ( ! _radio_cap_run_command(szComm, szOutput, RADIO_CAP_IW_OUTPUT_SIZE, NULL) )
   {
      log_softerror_and_alarm("Failed to read the capabilities of radio interface %d (%s, phy%d): %s",
         iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex, s_szLastError);
      free(szOutput);
      return 0;
   }

   if ( 0 == strlen(szOutput) )
   {
      _radio_cap_set_error("Failed to read the capabilities of radio interface %d (%s, phy%d)", iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex);
      log_softerror_and_alarm("Failed to read the capabilities of radio interface %d (%s, phy%d).", iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex);
      free(szOutput);
      return 0;
   }

   int iChannels = radio_cap_parse_iw_phy_info(szOutput, pOut);
   free(szOutput);

   if ( iChannels <= 0 )
   {
      _radio_cap_set_error("Failed to parse the capabilities of radio interface %d (%s, phy%d)", iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex);
      log_softerror_and_alarm("Failed to parse the capabilities of radio interface %d (%s, phy%d).", iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex);
      return 0;
   }

   log_line("[RF-CAP] Interface %d (%s, phy%d): %d bands, %d channels, monitor supported: %d, monitor enabled: %d, bands: [%s]",
      iRadioIndex+1, pRadioHWInfo->szName, pOut->iPhyIndex, pOut->iBandCount, iChannels,
      pOut->bMonitorSupported, pOut->bMonitorEnabled, radio_cap_interface_get_bands_description(pOut));

   return 1;
}


int radio_cap_probe_monitor_state(int iRadioIndex, int* piMonitorEnabled)
{
   if ( NULL != piMonitorEnabled )
      *piMonitorEnabled = 0;

   s_szLastError[0] = 0;

   if ( iRadioIndex < 0 )
   {
      _radio_cap_set_error("Invalid radio interface index");
      return -1;
   }

   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iRadioIndex);
   if ( NULL == pRadioHWInfo )
   {
      _radio_cap_set_error("Invalid radio interface %d", iRadioIndex+1);
      return -1;
   }

   if ( ! radio_cap_is_iw_available() )
   {
      _radio_cap_set_error("The 'iw' tool is not installed. Install the 'iw' package to use the WiFi radios.");
      return -1;
   }

   char* szOutput = (char*)malloc(RADIO_CAP_IW_OUTPUT_SIZE);
   if ( NULL == szOutput )
   {
      _radio_cap_set_error("Out of memory");
      return -1;
   }

   char szComm[256];
   snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), "%s dev 2>&1", radio_cap_get_iw_path());
   int iFound = _radio_cap_run_command(szComm, szOutput, RADIO_CAP_IW_OUTPUT_SIZE, NULL);
   if ( ! iFound )
      szOutput[0] = 0;

   int iMonitor = 0;
   int iPhy = radio_cap_parse_iw_dev_info(szOutput, pRadioHWInfo->szName, &iMonitor);
   free(szOutput);

   if ( iPhy < 0 )
   {
      _radio_cap_set_error("Failed to find the radio interface %d (%s)", iRadioIndex+1, pRadioHWInfo->szName);
      return -1;
   }

   if ( NULL != piMonitorEnabled )
      *piMonitorEnabled = iMonitor;

   return iPhy;
}


int radio_cap_set_monitor_mode(int iRadioIndex, int bEnabled)
{
   s_szLastError[0] = 0;

   if ( iRadioIndex < 0 )
   {
      _radio_cap_set_error("Invalid radio interface index");
      return 0;
   }

   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iRadioIndex);
   if ( NULL == pRadioHWInfo )
   {
      _radio_cap_set_error("Invalid radio interface %d", iRadioIndex+1);
      return 0;
   }

   // The interface that carries the network connection of the machine is never
   // changed: it is not a radio of the ground station and its mode cannot be
   // changed without breaking the network connection of the machine.
   if ( hardware_radio_is_protected_interface(iRadioIndex) )
   {
      _radio_cap_set_error("The interface %s carries the network connection of this machine and it is not changed", pRadioHWInfo->szName);
      log_line("[RF-CAP] The mode of radio interface %d (%s) is not changed: it carries the network connection of this machine.",
         iRadioIndex+1, pRadioHWInfo->szName);
      return 0;
   }

   if ( ! radio_cap_is_iw_available() )
   {
      _radio_cap_set_error("The 'iw' tool is not installed. Install the 'iw' package to use the WiFi radios.");
      log_softerror_and_alarm("Cannot set the monitor mode of radio interface %d: the 'iw' tool is not installed.", iRadioIndex+1);
      return 0;
   }

   char szComm[256];
   snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), "%s dev %s set type %s 2>&1",
      radio_cap_get_iw_path(), pRadioHWInfo->szName, bEnabled ? "monitor" : "managed");
   log_line("[RF-CAP] Setting radio interface %d (%s) to %s mode.", iRadioIndex+1, pRadioHWInfo->szName,
      bEnabled ? "monitor" : "managed");

   char* szOutput = (char*)malloc(RADIO_CAP_IW_OUTPUT_SIZE);
   if ( NULL == szOutput )
   {
      _radio_cap_set_error("Out of memory");
      return 0;
   }
   int iExitStatus = -1;
   int iFound = _radio_cap_run_command(szComm, szOutput, RADIO_CAP_IW_OUTPUT_SIZE, &iExitStatus);

   // The decision is based on the result of the command runner and on the
   // exit status of the command, and not only on a search in the output. A
   // command that failed was never reported as a success.
   int bSucceeded = radio_cap_command_succeeded(iFound, iExitStatus, szOutput);

   // The output is copied out of the heap buffer before it is released, the
   // error message is built from the copy.
   char szResult[512];
   snprintf(szResult, sizeof(szResult)/sizeof(szResult[0]), "%s", szOutput);
   free(szOutput);

   // The newlines are replaced only to make the message readable on one line.
   // The decision was already made, on the raw output.
   int iLen = strlen(szResult);
   for( int i=0; i<iLen; i++ )
      if ( (szResult[i] == 10) || (szResult[i] == 13) )
         szResult[i] = '.';

   if ( ! bSucceeded )
   {
      if ( (NULL != strstr(szResult, "command failed")) || (NULL != strstr(szResult, "Operation not permitted")) )
      {
         _radio_cap_set_error("Failed to set the %s mode of radio interface %d (%s): %s. "
            "The 'CAP_NET_ADMIN' capability is needed to change the mode of a WiFi interface.",
            bEnabled ? "monitor" : "managed", iRadioIndex+1, pRadioHWInfo->szName, szResult);
         log_softerror_and_alarm("Failed to set the %s mode of radio interface %d (%s): %s. "
            "The 'CAP_NET_ADMIN' capability is needed to change the mode of a WiFi interface.",
            bEnabled ? "monitor" : "managed", iRadioIndex+1, pRadioHWInfo->szName, szResult);
      }
      else
      {
         _radio_cap_set_error("Failed to set the %s mode of radio interface %d (%s): %s (exit status %d)",
            bEnabled ? "monitor" : "managed", iRadioIndex+1, pRadioHWInfo->szName, szResult, iExitStatus);
         log_softerror_and_alarm("Failed to set the %s mode of radio interface %d (%s): %s (exit status %d)",
            bEnabled ? "monitor" : "managed", iRadioIndex+1, pRadioHWInfo->szName, szResult, iExitStatus);
      }
      return 0;
   }

   log_line("[RF-CAP] Radio interface %d (%s) was set to %s mode.", iRadioIndex+1, pRadioHWInfo->szName,
      bEnabled ? "monitor" : "managed");
   return 1;
}


// Applies the Ruby specific monitor flags of an interface.
// The embedded platforms set the 'none' and the 'fcsfail' monitor flags, so the
// radiotap headers carry the same information as on the embedded hardware.
// The flags are not available on every driver, so a failure of a flag is
// reported but it does not undo the monitor mode, which is already set.
int radio_cap_set_monitor_flags(int iRadioIndex)
{
   s_szLastError[0] = 0;

   if ( iRadioIndex < 0 )
   {
      _radio_cap_set_error("Invalid radio interface index");
      return 0;
   }

   radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(iRadioIndex);
   if ( NULL == pRadioHWInfo )
   {
      _radio_cap_set_error("Invalid radio interface %d", iRadioIndex+1);
      return 0;
   }

   // The interface that carries the network connection of the machine is never
   // changed: it is not a radio of the ground station.
   if ( hardware_radio_is_protected_interface(iRadioIndex) )
   {
      _radio_cap_set_error("The interface %s carries the network connection of this machine and it is not changed", pRadioHWInfo->szName);
      return 0;
   }

   if ( ! radio_cap_is_iw_available() )
   {
      _radio_cap_set_error("The 'iw' tool is not installed. Install the 'iw' package to use the WiFi radios.");
      return 0;
   }

   static const char* s_szFlags[] = { "none", "fcsfail" };
   int iCountApplied = 0;
   for( int i=0; i<(int)(sizeof(s_szFlags)/sizeof(s_szFlags[0])); i++ )
   {
      char szComm[256];
      snprintf(szComm, sizeof(szComm)/sizeof(szComm[0]), "%s dev %s set monitor %s 2>&1",
         radio_cap_get_iw_path(), pRadioHWInfo->szName, s_szFlags[i]);

      char* szOutput = (char*)malloc(RADIO_CAP_IW_OUTPUT_SIZE);
      if ( NULL == szOutput )
      {
         _radio_cap_set_error("Out of memory");
         return 0;
      }
      int iExitStatus = -1;
      int iFound = _radio_cap_run_command(szComm, szOutput, RADIO_CAP_IW_OUTPUT_SIZE, &iExitStatus);

      // The decision is based on the result of the command runner and on the
      // exit status of the command, and not only on a search in the output.
      int bSucceeded = radio_cap_command_succeeded(iFound, iExitStatus, szOutput);

      int iLen = strlen(szOutput);
      for( int i=0; i<iLen; i++ )
         if ( (szOutput[i] == 10) || (szOutput[i] == 13) )
            szOutput[i] = '.';

      // The output is copied out of the heap buffer before it is released.
      char szResult[512];
      snprintf(szResult, sizeof(szResult)/sizeof(szResult[0]), "%s", szOutput);
      free(szOutput);

      if ( ! bSucceeded )
      {
         // The flag is not supported by this driver, the monitor mode is
         // still set, so this is a warning and not a failure.
         log_line("[RF-CAP] The monitor flag '%s' was not accepted by radio interface %d (%s): %s",
            s_szFlags[i], iRadioIndex+1, pRadioHWInfo->szName, szResult);
         continue;
      }
      iCountApplied++;
   }

   if ( 0 == iCountApplied )
   {
      _radio_cap_set_error("None of the Ruby monitor flags was accepted by radio interface %d (%s)",
         iRadioIndex+1, pRadioHWInfo->szName);
      return 0;
   }

   log_line("[RF-CAP] The Ruby monitor flags were applied to radio interface %d (%s).",
      iRadioIndex+1, pRadioHWInfo->szName);
   return 1;
}
