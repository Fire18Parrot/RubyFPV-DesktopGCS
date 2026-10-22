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

#pragma once
#include "menu_objects.h"
#include "menu_item_select.h"
#include "menu_item_text.h"
#include "menu_item_edit.h"
#include "menu_item_range.h"
#include "../../base/radio_capabilities.h"
#include "../../base/radio_profiles.h"

// Menu of the named RF profiles.
//
// A RF profile describes how a WiFi radio interface has to be configured:
// the interface, the role, the mode, the band, the frequency, the channel, the
// bandwidth and the transmit power. The profiles are stored in the configuration
// folder and they can be created, edited, duplicated, removed and applied to a
// radio interface.
//
// The menu has two modes:
//   - the list mode, where the stored profiles and the list commands are shown,
//   - the edit mode, where the fields of one profile are shown and edited.
class MenuRFProfiles: public Menu
{
   public:
      MenuRFProfiles();
      virtual ~MenuRFProfiles();
      virtual void onShow();
      virtual void valuesToUI();
      virtual void Render();
      virtual void onSelectItem();
      virtual int  onBack();
      virtual void onItemValueChanged(int itemIndex);

      // Reloads the profiles from the configuration file
      static void reloadProfiles();

   protected:
      void addItems();
      void addProfileItems();
      void addEditItems();
      int  _addSelectableText(const char* szText);
      void applyProfile(int iProfileIndex);
      void removeProfile(int iProfileIndex);
      void duplicateProfile(int iProfileIndex);
      void saveProfiles();

      // Leaves the edit mode. A new profile that was not saved is removed.
      void cancelEdit();

      // Reads the edited values back into a profile
      void readEditedProfile(radio_profile_t* pProfile);

      // Adds the selections of the channels of a band to a select item.
      // The channels are read from the capabilities of the given radio
      // interface. Returns the number of added selections.
      int  _addBandChannels(MenuItemSelect* pItem, int iBand, int iInterfaceIndex);

      // Returns the radio interface the edited profile is bound to, or the
      // interface an automatic profile would use. It returns -1 if there is no
      // usable interface.
      int  _getEditedInterfaceIndex();

      // The index of the profile that is selected in the list, or -1
      int  m_iSelectedProfile;

      // The index of the profile that is being edited, or -1 for the list mode
      int  m_iEditingProfile;

      // The band of the edited profile, used to build the channel list
      int  m_iEditingBand;

      // The index of the radio interface that is being edited, used to build
      // the channel list
      int  m_iEditingInterface;

      // The index of a profile that was added by the "New Profile" command and
      // that was not saved yet. It is removed if the edit is cancelled.
      int  m_iNewProfile;

      MenuItemEdit* m_pItemEditName;
      MenuItemSelect* m_pItemSelectInterface;
      MenuItemSelect* m_pItemSelectRole;
      MenuItemSelect* m_pItemSelectMode;
      MenuItemSelect* m_pItemSelectBand;
      MenuItemSelect* m_pItemSelectChannel;
      MenuItemRange* m_pItemRangeFrequency;
      MenuItemSelect* m_pItemSelectBandwidth;
      MenuItemSelect* m_pItemSelectTxPower;
      MenuItemSelect* m_pItemSelectProfileMaxTxPower;
      MenuItemSelect* m_pItemSelectEnabled;

      int m_iIndexName;
      int m_iIndexInterface;
      int m_iIndexRole;
      int m_iIndexMode;
      int m_iIndexBand;
      int m_iIndexChannel;
      int m_iIndexFrequency;
      int m_iIndexBandwidth;
      int m_iIndexTxPower;
      int m_iIndexProfileMaxTxPower;
      int m_iIndexEnabled;

      // the commands of the list
      int m_iIndexNew;
      int m_iIndexEdit;
      int m_iIndexDuplicate;
      int m_iIndexRemove;
      int m_iIndexApply;

      // the commands of the edit mode
      int m_iIndexSave;
      int m_iIndexCancel;
};
