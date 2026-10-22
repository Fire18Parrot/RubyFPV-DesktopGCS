/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of/or use in source and/or binary forms (partially or complete) must retain
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

#include "menu.h"
#include "menu_rf_profiles.h"
#include "menu_objects.h"
#include "menu_item_text.h"
#include "menu_item_edit.h"
#include "menu_item_range.h"
#include "menu_item_section.h"
#include "../../base/radio_capabilities.h"
#include "../../base/radio_profiles.h"
#include "../../base/hardware_radio.h"

// The commands of this menu
#define MRP_ID_PROFILE_NEW       1
#define MRP_ID_PROFILE_DUPLICATE 2
#define MRP_ID_PROFILE_REMOVE    3
#define MRP_ID_PROFILE_APPLY     4
#define MRP_ID_PROFILE_SAVE      5
#define MRP_ID_PROFILE_CANCEL    6
#define MRP_ID_PROFILE_EDIT      7

// The bands the UI offers, in the order they are displayed. The values are the
// real RADIO_HW_SUPPORTED_BAND_* bit masks, not a sequence.
static const int s_iBands[] = {
   RADIO_HW_SUPPORTED_BAND_23, RADIO_HW_SUPPORTED_BAND_24, RADIO_HW_SUPPORTED_BAND_25,
   RADIO_HW_SUPPORTED_BAND_58, RADIO_HW_SUPPORTED_BAND_433,
   RADIO_HW_SUPPORTED_BAND_868, RADIO_HW_SUPPORTED_BAND_915 };
static const int s_iBandsCount = (int)(sizeof(s_iBands)/sizeof(s_iBands[0]));

// The maximum number of channels added to the channel selector
#define MRP_MAX_CHANNEL_SELECTIONS 64

// The frequency range offered by the UI. The minimum is 0, which means the
// frequency is derived from the band and the channel. The maximum covers the
// 5.8 GHz band, so the valid 5 GHz frequencies can be entered.
#define MRP_FREQUENCY_MIN_MHZ 0
#define MRP_FREQUENCY_MAX_MHZ 5900

MenuRFProfiles::MenuRFProfiles()
:Menu(MENU_ID_RF_PROFILES, "RF Profiles", NULL)
{
   m_Width = 0.42;
   m_xPos = menu_get_XStartPos(m_Width);
   m_yPos = 0.12;

   m_iSelectedProfile = -1;
   m_iEditingProfile = -1;
   m_iEditingBand = 0;
   m_iEditingInterface = -1;
   m_iNewProfile = -1;

   m_pItemEditName = NULL;
   m_pItemSelectInterface = NULL;
   m_pItemSelectRole = NULL;
   m_pItemSelectMode = NULL;
   m_pItemSelectBand = NULL;
   m_pItemSelectChannel = NULL;
   m_pItemRangeFrequency = NULL;
   m_pItemSelectBandwidth = NULL;
   m_pItemSelectTxPower = NULL;
   m_pItemSelectProfileMaxTxPower = NULL;
   m_pItemSelectEnabled = NULL;

   m_iIndexName = -1;
   m_iIndexInterface = -1;
   m_iIndexRole = -1;
   m_iIndexMode = -1;
   m_iIndexBand = -1;
   m_iIndexChannel = -1;
   m_iIndexFrequency = -1;
   m_iIndexBandwidth = -1;
   m_iIndexTxPower = -1;
   m_iIndexProfileMaxTxPower = -1;
   m_iIndexEnabled = -1;

   m_iIndexNew = -1;
   m_iIndexEdit = -1;
   m_iIndexDuplicate = -1;
   m_iIndexRemove = -1;
   m_iIndexApply = -1;

   m_iIndexSave = -1;
   m_iIndexCancel = -1;
}

MenuRFProfiles::~MenuRFProfiles()
{
}

void MenuRFProfiles::reloadProfiles()
{
   radio_profiles_init();
   radio_profiles_load();
}

void MenuRFProfiles::onShow()
{
   int iTmp = getSelectedMenuItemIndex();

   reloadProfiles();
   m_iEditingProfile = -1;
   m_iNewProfile = -1;
   addItems();
   Menu::onShow();

   if ( iTmp > 0 )
      m_SelectedIndex = iTmp;
   if ( m_SelectedIndex >= m_ItemsCount )
      m_SelectedIndex = m_ItemsCount-1;
}

void MenuRFProfiles::valuesToUI()
{
   addItems();
}

void MenuRFProfiles::addItems()
{
   int iTmp = getSelectedMenuItemIndex();

   removeAllItems();

   m_pItemEditName = NULL;
   m_pItemSelectInterface = NULL;
   m_pItemSelectRole = NULL;
   m_pItemSelectMode = NULL;
   m_pItemSelectBand = NULL;
   m_pItemSelectChannel = NULL;
   m_pItemRangeFrequency = NULL;
   m_pItemSelectBandwidth = NULL;
   m_pItemSelectTxPower = NULL;
   m_pItemSelectProfileMaxTxPower = NULL;
   m_pItemSelectEnabled = NULL;

   m_iIndexName = -1;
   m_iIndexInterface = -1;
   m_iIndexRole = -1;
   m_iIndexMode = -1;
   m_iIndexBand = -1;
   m_iIndexChannel = -1;
   m_iIndexFrequency = -1;
   m_iIndexBandwidth = -1;
   m_iIndexTxPower = -1;
   m_iIndexProfileMaxTxPower = -1;
   m_iIndexEnabled = -1;

   m_iIndexNew = -1;
   m_iIndexEdit = -1;
   m_iIndexDuplicate = -1;
   m_iIndexRemove = -1;
   m_iIndexApply = -1;

   m_iIndexSave = -1;
   m_iIndexCancel = -1;

   if ( m_iEditingProfile >= 0 )
      addEditItems();
   else
      addProfileItems();

   if ( iTmp > 0 )
      m_SelectedIndex = iTmp;
   if ( m_SelectedIndex >= m_ItemsCount )
      m_SelectedIndex = m_ItemsCount-1;
}

// Adds a selectable text item and returns its index
int MenuRFProfiles::_addSelectableText(const char* szText)
{
   MenuItemText* pItem = new MenuItemText(szText);
   pItem->makeSelectable();
   return addMenuItem(pItem);
}

// Adds the channels of a band to a select item. The first selection is always
// the automatic one, the following ones are the channels of the band.
// The channels are read from the capabilities of the given radio interface.
// Returns the number of added selections.
int MenuRFProfiles::_addBandChannels(MenuItemSelect* pItem, int iBand, int iInterfaceIndex)
{
   pItem->addSelection("Automatic");

   if ( iInterfaceIndex < 0 )
      return 1;

   radio_cap_interface_t Cap;
   if ( ! radio_cap_probe_interface(iInterfaceIndex, &Cap) )
      return 1;

   const radio_cap_band_t* pBand = radio_cap_interface_get_band(&Cap, iBand);
   if ( NULL == pBand )
      return 1;

   int iAdded = 1;
   for( int i=0; i<pBand->iChannelCount; i++ )
   {
      const radio_cap_channel_t* pChannel = &(pBand->Channels[i]);
      if ( pChannel->bDisabled )
         continue;
      if ( iAdded >= MRP_MAX_CHANNEL_SELECTIONS )
         break;
      char szBuff[32];
      snprintf(szBuff, sizeof(szBuff), "%d", pChannel->iChannelNumber);
      pItem->addSelection(szBuff);
      iAdded++;
   }
   return iAdded;
}

// Returns the radio interface the edited profile is bound to, or the
// interface that would be used by an automatic profile. It returns -1 if there
// is no usable interface.
int MenuRFProfiles::_getEditedInterfaceIndex()
{
   const radio_profile_t* pProfile = radio_profiles_get(m_iEditingProfile);
   if ( NULL == pProfile )
      return -1;
   return radio_profiles_resolve_interface(pProfile);
}

void MenuRFProfiles::addProfileItems()
{
   addMenuItem(new MenuItemSection("RF Profiles"));

   int iCount = radio_profiles_get_count();
   if ( 0 == iCount )
   {
      addMenuItem(new MenuItemText("No RF profiles defined.", true));
   }
   else
   {
      for( int i=0; i<iCount; i++ )
      {
         const radio_profile_t* pProfile = radio_profiles_get(i);
         if ( NULL == pProfile )
            continue;
         char szBuff[128];
         snprintf(szBuff, sizeof(szBuff), "%s%s", pProfile->szName, pProfile->iEnabled ? "" : " (disabled)");
         _addSelectableText(szBuff);
      }
   }

   addMenuItem(new MenuItemText(" "));

   // the commands of the list
   m_iIndexNew = _addSelectableText("New Profile");
   m_iIndexEdit = (0 < iCount) ? _addSelectableText("Edit Selected Profile") : -1;
   m_iIndexDuplicate = (0 < iCount) ? _addSelectableText("Duplicate Selected Profile") : -1;
   m_iIndexRemove = (0 < iCount) ? _addSelectableText("Remove Selected Profile") : -1;
   m_iIndexApply = _addSelectableText("Apply Selected Profile");
}

void MenuRFProfiles::addEditItems()
{
   const radio_profile_t* pProfile = radio_profiles_get(m_iEditingProfile);
   if ( NULL == pProfile )
   {
      m_iEditingProfile = -1;
      addProfileItems();
      return;
   }

   radio_profile_t Profile = *pProfile;

   addMenuItem(new MenuItemSection("Edit RF Profile"));

   // --- the name ---
   m_pItemEditName = new MenuItemEdit("Profile Name", Profile.szName);
   m_pItemEditName->setMaxLength(RADIO_PROFILE_MAX_NAME_LENGTH);
   m_iIndexName = addMenuItem(m_pItemEditName);

   // --- the interface ---
   // Only the interfaces that can be configured by a profile are offered:
   // the WiFi interfaces. A serial radio is configured by its own driver and
   // not by an RF profile.
   m_pItemSelectInterface = new MenuItemSelect("Radio Interface");
   m_pItemSelectInterface->addSelection("Automatic");
   for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
         continue;
      char szBuff[128];
      snprintf(szBuff, sizeof(szBuff), "Interface %d (%s)", i+1, pRadioHWInfo->szName);
      m_pItemSelectInterface->addSelection(szBuff);
   }
   m_pItemSelectInterface->setIsEditable();
   m_pItemSelectInterface->setPopupSelectorToRight();
   m_pItemSelectInterface->setSelectedIndex(0);
   if ( Profile.iInterfaceIndex >= 0 )
   {
      // The selection index is the position of the interface in the list of
      // the WiFi interfaces, plus one for the automatic entry. The position is
      // counted over the WiFi interfaces only, the hardware index of the
      // interface is not the position in this list.
      int iSel = 0;
      int iWiFi = 0;
      for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
      {
         radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
         if ( NULL == pRadioHWInfo )
            continue;
         if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
            continue;
         if ( i == Profile.iInterfaceIndex )
         {
            iSel = iWiFi+1;
            break;
         }
         iWiFi++;
      }
      if ( iSel < m_pItemSelectInterface->getSelectionsCount() )
         m_pItemSelectInterface->setSelectedIndex(iSel);
   }
   m_iIndexInterface = addMenuItem(m_pItemSelectInterface);

   // --- the role ---
   m_pItemSelectRole = new MenuItemSelect("Role");
   for( int i=0; i<RADIO_CAP_ROLE_COUNT; i++ )
      m_pItemSelectRole->addSelection(radio_profiles_get_role_description(i));
   m_pItemSelectRole->setIsEditable();
   m_pItemSelectRole->setPopupSelectorToRight();
   if ( (Profile.iRole >= 0) && (Profile.iRole < RADIO_CAP_ROLE_COUNT) )
      m_pItemSelectRole->setSelectedIndex(Profile.iRole);
   m_iIndexRole = addMenuItem(m_pItemSelectRole);

   // --- the mode ---
   m_pItemSelectMode = new MenuItemSelect("Operating Mode");
   for( int i=0; i<RADIO_CAP_MODE_COUNT; i++ )
      m_pItemSelectMode->addSelection(radio_profiles_get_mode_description(i));
   m_pItemSelectMode->setIsEditable();
   m_pItemSelectMode->setPopupSelectorToRight();
   if ( (Profile.iMode >= 0) && (Profile.iMode < RADIO_CAP_MODE_COUNT) )
      m_pItemSelectMode->setSelectedIndex(Profile.iMode);
   m_iIndexMode = addMenuItem(m_pItemSelectMode);

   // --- the band ---
   m_pItemSelectBand = new MenuItemSelect("Band");
   m_pItemSelectBand->addSelection("Automatic");
   for( int i=0; i<s_iBandsCount; i++ )
      m_pItemSelectBand->addSelection(radio_profiles_get_band_description(s_iBands[i]));
   m_pItemSelectBand->setIsEditable();
   m_pItemSelectBand->setPopupSelectorToRight();
   m_pItemSelectBand->setSelectedIndex(0);
   for( int i=0; i<s_iBandsCount; i++ )
      if ( s_iBands[i] == Profile.iBand )
      {
         m_pItemSelectBand->setSelectedIndex(i+1);
         break;
      }
   m_iIndexBand = addMenuItem(m_pItemSelectBand);
   m_iEditingBand = Profile.iBand;

   // --- the channel ---
   // The channels are the ones reported by the capabilities of the interface
   // the profile is bound to. The interface is the one of the profile, or the
   // one an automatic profile would use.
   m_iEditingInterface = _getEditedInterfaceIndex();
   m_pItemSelectChannel = new MenuItemSelect("Channel");
   m_iIndexChannel = -1;
   if ( 0 != Profile.iBand )
   {
      _addBandChannels(m_pItemSelectChannel, Profile.iBand, m_iEditingInterface);
      m_pItemSelectChannel->setIsEditable();
      m_pItemSelectChannel->setPopupSelectorToRight();
      m_pItemSelectChannel->setSelectedIndex(0);
      for( int i=1; i<m_pItemSelectChannel->getSelectionsCount(); i++ )
      {
         const char* szSel = m_pItemSelectChannel->getSelectionIndexText(i);
         if ( NULL == szSel )
            continue;
         if ( Profile.iChannel == atoi(szSel) )
         {
            m_pItemSelectChannel->setSelectedIndex(i);
            break;
         }
      }
      m_iIndexChannel = addMenuItem(m_pItemSelectChannel);
   }
   else
   {
      // without a band the channel cannot be picked, the channel is derived
      // from the frequency
      addMenuItem(new MenuItemText("Channel: derived from the frequency", true));
   }

   // --- the frequency ---
   // The frequency is entered in MHz. The value 0 means automatic, the
   // frequency is then derived from the band and the channel.
   m_pItemRangeFrequency = new MenuItemRange("Frequency (MHz)", MRP_FREQUENCY_MIN_MHZ, MRP_FREQUENCY_MAX_MHZ, 0, 1);
   m_pItemRangeFrequency->setSufix(" MHz");
   if ( 0 != Profile.uFrequencyKhz )
      m_pItemRangeFrequency->setCurrentValue((float)(Profile.uFrequencyKhz/1000));
   m_iIndexFrequency = addMenuItem(m_pItemRangeFrequency);

   // --- the bandwidth ---
   m_pItemSelectBandwidth = new MenuItemSelect("Bandwidth");
   for( int i=0; i<RADIO_CAP_BW_COUNT; i++ )
      m_pItemSelectBandwidth->addSelection(radio_profiles_get_bandwidth_description(i));
   m_pItemSelectBandwidth->setIsEditable();
   m_pItemSelectBandwidth->setPopupSelectorToRight();
   if ( (Profile.iBandwidthMode >= 0) && (Profile.iBandwidthMode < RADIO_CAP_BW_COUNT) )
      m_pItemSelectBandwidth->setSelectedIndex(Profile.iBandwidthMode);
   m_iIndexBandwidth = addMenuItem(m_pItemSelectBandwidth);

   // --- the TX power ---
   m_pItemSelectTxPower = new MenuItemSelect("Configured TX Power (dBm)");
   for( int i=0; i<=30; i++ )
   {
      char szBuff[32];
      snprintf(szBuff, sizeof(szBuff), "%d", i);
      m_pItemSelectTxPower->addSelection(szBuff);
   }
   m_pItemSelectTxPower->setIsEditable();
   m_pItemSelectTxPower->setPopupSelectorToRight();
   m_pItemSelectTxPower->setSelectedIndex(0);
   if ( (Profile.iConfiguredTxPowerDbm >= 0) && (Profile.iConfiguredTxPowerDbm < m_pItemSelectTxPower->getSelectionsCount()) )
      m_pItemSelectTxPower->setSelectedIndex(Profile.iConfiguredTxPowerDbm);
   m_iIndexTxPower = addMenuItem(m_pItemSelectTxPower);

   // --- the profile maximum TX power ---
   m_pItemSelectProfileMaxTxPower = new MenuItemSelect("Profile Max TX Power (dBm)");
   m_pItemSelectProfileMaxTxPower->addSelection("No limit");
   for( int i=0; i<=30; i++ )
   {
      char szBuff[32];
      snprintf(szBuff, sizeof(szBuff), "%d", i);
      m_pItemSelectProfileMaxTxPower->addSelection(szBuff);
   }
   m_pItemSelectProfileMaxTxPower->setIsEditable();
   m_pItemSelectProfileMaxTxPower->setPopupSelectorToRight();
   m_pItemSelectProfileMaxTxPower->setSelectedIndex(0);
   if ( Profile.iProfileMaxTxPowerDbm > 0 )
   {
      if ( (Profile.iProfileMaxTxPowerDbm+1) < m_pItemSelectProfileMaxTxPower->getSelectionsCount() )
         m_pItemSelectProfileMaxTxPower->setSelectedIndex(Profile.iProfileMaxTxPowerDbm+1);
   }
   m_iIndexProfileMaxTxPower = addMenuItem(m_pItemSelectProfileMaxTxPower);

   // --- the enabled flag ---
   m_pItemSelectEnabled = new MenuItemSelect("Profile Enabled");
   m_pItemSelectEnabled->addSelection("Disabled");
   m_pItemSelectEnabled->addSelection("Enabled");
   m_pItemSelectEnabled->setIsEditable();
   m_pItemSelectEnabled->setPopupSelectorToRight();
   m_pItemSelectEnabled->setSelectedIndex(Profile.iEnabled ? 1 : 0);
   m_iIndexEnabled = addMenuItem(m_pItemSelectEnabled);

   addMenuItem(new MenuItemText(" "));
   m_iIndexSave = _addSelectableText("Save Profile");
   m_iIndexCancel = _addSelectableText("Cancel");
}

void MenuRFProfiles::readEditedProfile(radio_profile_t* pProfile)
{
   if ( NULL == pProfile )
      return;

   if ( NULL != m_pItemEditName )
   {
      const char* szName = m_pItemEditName->getCurrentValue();
      if ( NULL != szName )
      {
         // the name is trimmed and it has to be not empty
         char szBuff[RADIO_PROFILE_MAX_NAME_LENGTH+1];
         strncpy(szBuff, szName, RADIO_PROFILE_MAX_NAME_LENGTH);
         szBuff[RADIO_PROFILE_MAX_NAME_LENGTH] = 0;
         int iStart = 0;
         while ( (szBuff[iStart] == ' ') && (0 != szBuff[iStart]) )
            iStart++;
         int iEnd = (int)strlen(szBuff)-1;
         while ( (iEnd >= iStart) && (szBuff[iEnd] == ' ') )
            szBuff[iEnd--] = 0;
         if ( iStart > 0 )
            memmove(szBuff, &(szBuff[iStart]), strlen(&(szBuff[iStart]))+1);
         if ( 0 == szBuff[0] )
            strcpy(szBuff, "Unnamed Profile");
         strcpy(pProfile->szName, szBuff);
      }
   }

   if ( NULL != m_pItemSelectInterface )
   {
      int iSel = m_pItemSelectInterface->getSelectedIndex();
      if ( 0 == iSel )
         pProfile->iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
      else
      {
         // the selection index is the index in the list of the WiFi interfaces
         int iIndex = 0;
         int iWiFi = 0;
         for( int i=0; i<hardware_get_radio_interfaces_count(); i++ )
         {
            radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
            if ( NULL == pRadioHWInfo )
               continue;
            if ( ! hardware_radio_is_wifi_radio(pRadioHWInfo) )
               continue;
            if ( iWiFi == (iSel-1) )
            {
               iIndex = i;
               break;
            }
            iWiFi++;
         }
         pProfile->iInterfaceIndex = (iIndex >= 0) ? iIndex : RADIO_PROFILE_IFACE_AUTO;
      }
   }

   if ( NULL != m_pItemSelectRole )
   {
      int iSel = m_pItemSelectRole->getSelectedIndex();
      if ( (iSel >= 0) && (iSel < RADIO_CAP_ROLE_COUNT) )
         pProfile->iRole = iSel;
   }

   if ( NULL != m_pItemSelectMode )
   {
      int iSel = m_pItemSelectMode->getSelectedIndex();
      if ( (iSel >= 0) && (iSel < RADIO_CAP_MODE_COUNT) )
         pProfile->iMode = iSel;
   }

   if ( NULL != m_pItemSelectBand )
   {
      int iSel = m_pItemSelectBand->getSelectedIndex();
      pProfile->iBand = 0;
      if ( (iSel > 0) && (iSel <= s_iBandsCount) )
         pProfile->iBand = s_iBands[iSel-1];
   }

   if ( NULL != m_pItemSelectChannel )
   {
      int iSel = m_pItemSelectChannel->getSelectedIndex();
      if ( (iSel <= 0) || (iSel >= m_pItemSelectChannel->getSelectionsCount()) )
         pProfile->iChannel = 0;
      else
      {
         const char* szSel = m_pItemSelectChannel->getSelectionIndexText(iSel);
         pProfile->iChannel = (NULL != szSel) ? atoi(szSel) : 0;
      }
   }
   else
      pProfile->iChannel = 0;

   if ( NULL != m_pItemRangeFrequency )
   {
      int iMhz = (int)(m_pItemRangeFrequency->getCurrentValue() + 0.5);
      pProfile->uFrequencyKhz = (iMhz > 0) ? ((u32)(iMhz*1000)) : 0;
   }

   if ( NULL != m_pItemSelectBandwidth )
   {
      int iSel = m_pItemSelectBandwidth->getSelectedIndex();
      if ( (iSel >= 0) && (iSel < RADIO_CAP_BW_COUNT) )
         pProfile->iBandwidthMode = iSel;
   }

   if ( NULL != m_pItemSelectTxPower )
   {
      int iSel = m_pItemSelectTxPower->getSelectedIndex();
      if ( (iSel >= 0) && (iSel < m_pItemSelectTxPower->getSelectionsCount()) )
         pProfile->iConfiguredTxPowerDbm = iSel;
   }

   if ( NULL != m_pItemSelectProfileMaxTxPower )
   {
      int iSel = m_pItemSelectProfileMaxTxPower->getSelectedIndex();
      pProfile->iProfileMaxTxPowerDbm = (0 == iSel) ? 0 : (iSel-1);
   }

   if ( NULL != m_pItemSelectEnabled )
      pProfile->iEnabled = (m_pItemSelectEnabled->getSelectedIndex() > 0) ? 1 : 0;
}

void MenuRFProfiles::onItemValueChanged(int itemIndex)
{
   if ( m_iEditingProfile < 0 )
      return;

   // The band changes the list of the channels and the interface changes the
   // capabilities that are read, so the items have to be rebuilt.
   if ( (itemIndex == m_iIndexBand) || (itemIndex == m_iIndexInterface) )
   {
      int iSel = getSelectedMenuItemIndex();
      radio_profile_t Profile;
      const radio_profile_t* pOld = radio_profiles_get(m_iEditingProfile);
      if ( NULL == pOld )
         return;
      Profile = *pOld;
      readEditedProfile(&Profile);
      radio_profiles_update(m_iEditingProfile, &Profile);
      addItems();
      if ( (iSel > 0) && (iSel < m_ItemsCount) )
         m_SelectedIndex = iSel;
   }
}

void MenuRFProfiles::onSelectItem()
{
   int iIndex = m_SelectedIndex;

   if ( m_iEditingProfile >= 0 )
   {
      if ( iIndex == m_iIndexSave )
      {
         const radio_profile_t* pOld = radio_profiles_get(m_iEditingProfile);
         if ( NULL == pOld )
            return;
         radio_profile_t Profile = *pOld;
         readEditedProfile(&Profile);

         if ( 1 == radio_profiles_update(m_iEditingProfile, &Profile) )
         {
            saveProfiles();
            // The profile was saved, it is not a new unsaved profile anymore.
            m_iNewProfile = -1;
            m_iEditingProfile = -1;
            addItems();
         }
         else
            addMessage(0, "Failed to update the RF profile.");
         return;
      }
      if ( iIndex == m_iIndexCancel )
      {
         cancelEdit();
         return;
      }
      return;
   }

   // the list mode.
   // The first item is the section title, the profiles start at the index 1.
   int iCount = radio_profiles_get_count();
   int iProfileIndex = radio_profiles_index_from_menu_index(iIndex, iCount);
   if ( iProfileIndex >= 0 )
   {
      // One of the profiles. The menu index is the profile index plus one.
      // The profile is selected, the edit mode is entered with the
      // "Edit Selected Profile" command.
      m_iSelectedProfile = iProfileIndex;
      addItems();
      return;
   }

   if ( iIndex == m_iIndexNew )
   {
      radio_profile_t Profile;
      radio_profiles_get_default_profile(&Profile);
      snprintf(Profile.szName, sizeof(Profile.szName), "Profile %d", iCount+1);
      int iNew = radio_profiles_add(&Profile);
      if ( iNew < 0 )
      {
         addMessage(0, "Failed to add the RF profile.");
         return;
      }
      m_iSelectedProfile = iNew;
      m_iEditingProfile = iNew;
      // The new profile is not saved yet, it is removed if the edit is
      // cancelled.
      m_iNewProfile = iNew;
      addItems();
      return;
   }

   if ( iIndex == m_iIndexEdit )
   {
      if ( (m_iSelectedProfile < 0) || (m_iSelectedProfile >= radio_profiles_get_count()) )
      {
         addMessage(0, "No RF profile selected. Select a profile first.");
         return;
      }
      m_iEditingProfile = m_iSelectedProfile;
      m_iNewProfile = -1;
      addItems();
      return;
   }

   if ( iIndex == m_iIndexDuplicate )
   {
      duplicateProfile(m_iSelectedProfile);
      return;
   }

   if ( iIndex == m_iIndexRemove )
   {
      removeProfile(m_iSelectedProfile);
      return;
   }

   if ( iIndex == m_iIndexApply )
   {
      applyProfile(m_iSelectedProfile);
      return;
   }
}

int MenuRFProfiles::onBack()
{
   // in the edit mode the back key cancels the edit, unless an item is being
   // edited, in which case the edit has to end first
   if ( m_iEditingProfile >= 0 )
   {
      if ( isEditingItem() )
         return Menu::onBack();

      cancelEdit();
      return 1;
   }
   return Menu::onBack();
}

// Leaves the edit mode. A profile that was added by the "New Profile" command
// and that was not saved is removed.
void MenuRFProfiles::cancelEdit()
{
   if ( (m_iNewProfile >= 0) && (m_iNewProfile == m_iEditingProfile) )
   {
      radio_profiles_remove(m_iNewProfile);
      m_iSelectedProfile = -1;
   }
   m_iNewProfile = -1;
   m_iEditingProfile = -1;
   addItems();
}

void MenuRFProfiles::applyProfile(int iProfileIndex)
{
   if ( (iProfileIndex < 0) || (iProfileIndex >= radio_profiles_get_count()) )
   {
      addMessage(0, "No RF profile selected. Select a profile first.");
      return;
   }

   char szError[256];
   szError[0] = 0;
   if ( ! radio_profiles_apply(iProfileIndex, szError, sizeof(szError)) )
   {
      if ( 0 == strlen(szError) )
         strcpy(szError, "Unknown error.");
      addMessageWithTitle(0, "Failed to apply the RF profile", szError);
      return;
   }
   addMessage(0, "The RF profile was applied.");
}

void MenuRFProfiles::removeProfile(int iProfileIndex)
{
   if ( (iProfileIndex < 0) || (iProfileIndex >= radio_profiles_get_count()) )
   {
      addMessage(0, "No RF profile selected. Select a profile first.");
      return;
   }
   if ( ! radio_profiles_remove(iProfileIndex) )
   {
      addMessage(0, "Failed to remove the RF profile.");
      return;
   }
   m_iSelectedProfile = -1;
   saveProfiles();
   addItems();
}

void MenuRFProfiles::duplicateProfile(int iProfileIndex)
{
   if ( (iProfileIndex < 0) || (iProfileIndex >= radio_profiles_get_count()) )
   {
      addMessage(0, "No RF profile selected. Select a profile first.");
      return;
   }
   int iNew = radio_profiles_duplicate(iProfileIndex, NULL);
   if ( iNew < 0 )
   {
      addMessage(0, "Failed to duplicate the RF profile.");
      return;
   }
   m_iSelectedProfile = iNew;
   saveProfiles();
   addItems();
}

void MenuRFProfiles::saveProfiles()
{
   if ( ! radio_profiles_save() )
   {
      addMessage(0, "Failed to save the RF profiles.");
      return;
   }
}

void MenuRFProfiles::Render()
{
   Menu::Render();
}
