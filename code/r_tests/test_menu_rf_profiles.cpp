/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of/or use in source or binary forms (partially or complete) must retain
          the above copyright notice, this list of conditions and the following disclaimer
          in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form must reproduce the above copyright notice, this
          list of conditions and the following disclaimer in the documentation and/or other
          materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the
          user interface, additions could be made to that info.
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

// Tests of the RF profiles user interface object.
//
// The test drives the MenuRFProfiles object directly, without a display and
// without a renderer. The object is constructed, its items are built and the
// commands are simulated by selecting the menu items and calling the handler
// methods.
//
// The test does not apply any profile to the hardware and it does not change
// the mode of any radio interface. The only hardware access is the read only
// enumeration of the radio interfaces, that is needed to build the lists of
// the channels of a band.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "../r_central/menu/menu_rf_profiles.h"
#include "../r_central/menu/menu_objects.h"
#include "../r_central/menu/menu_item_select.h"
#include "../r_central/menu/menu_item_range.h"
#include "../r_central/menu/menu_item_edit.h"
#include "../renderer/render_engine.h"
#include "../base/radio_profiles.h"
#include "../base/radio_capabilities.h"
#include "../base/hardware_radio.h"

// The render engine of the user interface. It is defined in the shared
// variables of the central application.
extern RenderEngine* g_pRenderEngine;

// A render engine that does not open a window.
//
// The menu items read the height of the text from the render engine when they
// are built, so a render engine has to exist. The test does not draw
// anything, so an engine without a window and without a draw context is
// enough. The size of the screen is set, so the height of the text is a
// usable value.
class RenderEngineHeadless: public RenderEngine
{
   public:
      bool initEngine()
      {
         m_iRenderWidth = 1280;
         m_iRenderHeight = 720;
         m_fPixelWidth = 1.0 / (float)m_iRenderWidth;
         m_fPixelHeight = 1.0 / (float)m_iRenderHeight;
         return true;
      }
};

static int s_iTests = 0;
static int s_iFailures = 0;

static void _check(const char* szName, int iGot, int iExpected)
{
   s_iTests++;
   if ( iGot != iExpected )
   {
      s_iFailures++;
      printf("  FAIL  %-55s got=%d expected=%d\n", szName, iGot, iExpected);
   }
   else
      printf("  PASS  %-55s got=%d\n", szName, iGot);
}

static void _check_str(const char* szName, const char* szGot, const char* szExpected)
{
   s_iTests++;
   if ( (NULL == szGot) || (NULL == szExpected) || (0 != strcmp(szGot, szExpected)) )
   {
      s_iFailures++;
      printf("  FAIL  %-55s got='%s' expected='%s'\n", szName,
         (NULL != szGot) ? szGot : "(null)", (NULL != szExpected) ? szExpected : "(null)");
   }
   else
      printf("  PASS  %-55s got='%s'\n", szName, szGot);
}


// Returns the index of the menu item with the given text, or -1.
static int _find_item(MenuRFProfiles* pMenu, const char* szText)
{
   for( int i=0; i<pMenu->getItemsCount(); i++ )
   {
      MenuItem* pItem = pMenu->getMenuItem(i);
      if ( NULL == pItem )
         continue;
      if ( NULL == pItem->getTitle() )
         continue;
      if ( 0 == strcmp(pItem->getTitle(), szText) )
         return i;
   }
   return -1;
}


// The list of the profiles is the section title at the index 0 and the
// profiles start at the index 1. The test uses the text of the items, so it
// does not depend on the exact position of the commands.
static void _select_item(MenuRFProfiles* pMenu, const char* szText)
{
   int iIndex = _find_item(pMenu, szText);
   if ( iIndex < 0 )
   {
      printf("  INFO  the menu item '%s' was not found\n", szText);
      return;
   }
   pMenu->setSelectedMenuItemIndex(iIndex);
   pMenu->onSelectItem();
}


// The behaviour of the RF profiles menu object:
//   - selecting a profile in the list does not enter the edit mode
//   - the "Edit Selected Profile" command enters the edit mode
//   - a band or an interface change rebuilds the list of the channels
//   - the 5 GHz frequencies can be reached
//   - the cancel command rolls back a new profile
//   - the save command persists a new profile
static void _test_menu_rf_profiles()
{
   printf("\n== Test: the RF profiles menu object ==\n");

   // The test uses its own profiles file, the real configuration file of the
   // user is not touched.
   const char* szTestFile = "/tmp/ruby_test_menu_rf_profiles.cfg";
   setenv("RUBY_RF_PROFILES_FILE", szTestFile, 1);
   unlink(szTestFile);

   radio_profiles_init();
   radio_profiles_remove(0);

   // A profile to work with.
   radio_profile_t Profile;
   radio_profiles_get_default_profile(&Profile);
   strcpy(Profile.szName, "Menu test profile");
   Profile.iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.uFrequencyKhz = 5180000;
   Profile.iChannel = 36;
   Profile.iRole = RADIO_CAP_ROLE_TXRX;
   Profile.iMode = RADIO_CAP_MODE_STATION;
   int iProfile = radio_profiles_add(&Profile);
   _check("the profile was added", iProfile, 0);
   _check("the profile count is one", radio_profiles_get_count(), 1);

   // The menu reloads the profiles from the file when it is shown, so the
   // profile has to be saved before the menu is built.
   _check("the profile was saved", radio_profiles_save(), 1);

   MenuRFProfiles* pMenu = new MenuRFProfiles();
   pMenu->onShow();

   // The list mode has the profile and the commands.
   _check("the list has the profile", _find_item(pMenu, "Menu test profile") > 0, 1);
   _check("the list has the new command", _find_item(pMenu, "New Profile") >= 0, 1);
   _check("the list has the edit command", _find_item(pMenu, "Edit Selected Profile") >= 0, 1);
   _check("the list has the apply command", _find_item(pMenu, "Apply Selected Profile") >= 0, 1);
   _check("the list has no save command", _find_item(pMenu, "Save Profile") < 0, 1);
   _check("the list has no cancel command", _find_item(pMenu, "Cancel") < 0, 1);

   // Selecting a profile in the list does not enter the edit mode. The edit
   // mode is entered only by the "Edit Selected Profile" command.
   _select_item(pMenu, "Menu test profile");
   _check("selecting a profile does not enter the edit mode",
      _find_item(pMenu, "Save Profile") < 0, 1);
   _check("selecting a profile keeps the list mode",
      _find_item(pMenu, "Edit Selected Profile") >= 0, 1);

   // The "Edit Selected Profile" command enters the edit mode.
   _select_item(pMenu, "Edit Selected Profile");
   _check("the edit command enters the edit mode",
      _find_item(pMenu, "Save Profile") >= 0, 1);
   _check("the edit mode has the cancel command",
      _find_item(pMenu, "Cancel") >= 0, 1);
   _check("the edit mode has the band item",
      _find_item(pMenu, "Band") >= 0, 1);
   _check("the edit mode has the frequency item",
      _find_item(pMenu, "Frequency (MHz)") >= 0, 1);
   _check("the edit mode has the interface item",
      _find_item(pMenu, "Radio Interface") >= 0, 1);
   _check("the edit mode has the channel item",
      _find_item(pMenu, "Channel") >= 0, 1);
   _check("the edit mode has no list command",
      _find_item(pMenu, "New Profile") < 0, 1);

   // The frequency range reaches the 5 GHz band.
   {
      MenuItemRange* pRange = (MenuItemRange*)pMenu->getMenuItem(_find_item(pMenu, "Frequency (MHz)"));
      _check("the frequency item is a range", (NULL != pRange) ? 1 : 0, 1);
      if ( NULL != pRange )
      {
         printf("  INFO  the frequency range is %d to %d MHz, the profile is at %d MHz\n",
            (int)pRange->getMinValue(), (int)pRange->getMaxValue(), (int)pRange->getCurrentValue());
         _check("the frequency range reaches the 5 GHz band",
            (pRange->getMaxValue() >= 5900) ? 1 : 0, 1);
         _check("the frequency range starts at the automatic value",
            (pRange->getMinValue() <= 0) ? 1 : 0, 1);
         // The profile frequency is shown in the item.
         _check("the frequency item shows the profile frequency",
            (int)pRange->getCurrentValue(), 5180);
      }
   }

   // The channel list of the 5.8 GHz band is built from the capabilities of
   // the interface. If there is no usable interface, the list has only the
   // automatic entry.
   {
      int iChannelItem = _find_item(pMenu, "Channel");
      MenuItemSelect* pChannel = (MenuItemSelect*)pMenu->getMenuItem(iChannelItem);
      _check("the channel item is a select", (NULL != pChannel) ? 1 : 0, 1);
      if ( NULL != pChannel )
      {
         printf("  INFO  the channel list has %d entries\n", pChannel->getSelectionsCount());
         _check("the channel list has the automatic entry",
            (pChannel->getSelectionsCount() >= 1) ? 1 : 0, 1);
         _check_str("the first channel entry is the automatic one",
            pChannel->getSelectionIndexText(0), "Automatic");
         // The profile is on channel 36, so the selection is on it.
         int iSel = pChannel->getSelectedIndex();
         _check("the channel selection is on the profile channel",
            (NULL != pChannel->getSelectionIndexText(iSel)) ? (36 == atoi(pChannel->getSelectionIndexText(iSel))) : 0, 1);
      }
   }

   // A band change rebuilds the items of the edit mode. The list of the
   // channels of the new band has to be built again.
   {
      int iBandItem = _find_item(pMenu, "Band");
      MenuItemSelect* pBand = (MenuItemSelect*)pMenu->getMenuItem(iBandItem);
      _check("the band item is a select", (NULL != pBand) ? 1 : 0, 1);
      if ( NULL != pBand )
      {
         // The band is selected in the list of the bands. The 5.8 GHz band
         // is the 4th band of the list, the first entry is the automatic one.
         int iBefore = 0;
         for( int i=1; i<pBand->getSelectionsCount(); i++ )
            if ( 0 == strcmp(pBand->getSelectionIndexText(i), "2.4GHz") )
               iBefore = i;
         _check("the 2.4 GHz band is in the band list", iBefore > 0, 1);

         // The 2.4 GHz band is selected. The items have to be rebuilt.
         pBand->setSelectedIndex(iBefore);
         pMenu->onItemValueChanged(iBandItem);

         _check("the band change rebuilds the edit mode",
            _find_item(pMenu, "Save Profile") >= 0, 1);
         MenuItemSelect* pBand2 = (MenuItemSelect*)pMenu->getMenuItem(_find_item(pMenu, "Band"));
         _check("the band selection is the new band", (NULL != pBand2) ? pBand2->getSelectedIndex() : -1, iBefore);
         MenuItemSelect* pChannel2 = (MenuItemSelect*)pMenu->getMenuItem(_find_item(pMenu, "Channel"));
         _check("the channel list was rebuilt", (NULL != pChannel2) ? 1 : 0, 1);
         if ( NULL != pChannel2 )
         {
            printf("  INFO  the channel list of the 2.4 GHz band has %d entries\n", pChannel2->getSelectionsCount());
            _check_str("the first channel entry is still the automatic one",
               pChannel2->getSelectionIndexText(0), "Automatic");
         }
      }
   }

   // The cancel command leaves the edit mode.
   _select_item(pMenu, "Cancel");
   _check("the cancel command leaves the edit mode",
      _find_item(pMenu, "Save Profile") < 0, 1);
   _check("the cancel command returns to the list mode",
      _find_item(pMenu, "Edit Selected Profile") >= 0, 1);

   // A new profile that is not saved is rolled back by the cancel command.
   {
      int iBefore = radio_profiles_get_count();
      _select_item(pMenu, "New Profile");
      _check("the new command enters the edit mode",
         _find_item(pMenu, "Save Profile") >= 0, 1);
      _check("the new profile was added", radio_profiles_get_count(), iBefore+1);

      // The name of the new profile is changed.
      MenuItemEdit* pName = (MenuItemEdit*)pMenu->getMenuItem(_find_item(pMenu, "Profile Name"));
      if ( NULL != pName )
      {
         pName->setCurrentValue("Rolled back profile");
      }

      _select_item(pMenu, "Cancel");
      _check("the cancel command rolls back the new profile",
         radio_profiles_get_count(), iBefore);
      _check("the rolled back profile is not in the list",
         _find_item(pMenu, "Rolled back profile") < 0, 1);
   }

   // A new profile is persisted by the save command.
   {
      int iBefore = radio_profiles_get_count();
      _select_item(pMenu, "New Profile");
      _check("the new command enters the edit mode again",
         _find_item(pMenu, "Save Profile") >= 0, 1);

      MenuItemEdit* pName = (MenuItemEdit*)pMenu->getMenuItem(_find_item(pMenu, "Profile Name"));
      if ( NULL != pName )
         pName->setCurrentValue("Saved menu profile");

      _select_item(pMenu, "Save Profile");
      _check("the save command keeps the profile", radio_profiles_get_count(), iBefore+1);
      _check("the save command leaves the edit mode",
         _find_item(pMenu, "Save Profile") < 0, 1);
      _check("the saved profile is in the list",
         _find_item(pMenu, "Saved menu profile") >= 0, 1);

      // The profile is in the file, it survives a reload.
      radio_profiles_init();
      int iCount = radio_profiles_load();
      printf("  INFO  %d profile(s) loaded from the file\n", iCount);
      int iFound = radio_profiles_find_by_name("Saved menu profile");
      _check("the saved profile was persisted", iFound, iBefore);
   }

   delete pMenu;
   radio_profiles_init();
   radio_profiles_remove(0);
   unlink(szTestFile);
}


int main(int argc, char* argv[])
{
   printf("\n=========================================================\n");
   printf("Tests for the RF profiles menu object\n");
   printf("=========================================================\n");

   // The menu items need a render engine to compute the height of the text.
   // The test uses an engine without a window, it does not draw anything.
   RenderEngineHeadless* pEngine = new RenderEngineHeadless();
   pEngine->initEngine();
   g_pRenderEngine = pEngine;

   _test_menu_rf_profiles();

   printf("\n=========================================================\n");
   printf("Tests: %d, failures: %d\n", s_iTests, s_iFailures);
   if ( 0 == s_iFailures )
      printf("ALL TESTS PASSED\n");
   else
      printf("SOME TESTS FAILED\n");
   printf("=========================================================\n");
   return (0 == s_iFailures) ? 0 : 1;
}
