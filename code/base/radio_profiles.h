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

#pragma once

#include "config.h"
#include "config_radio.h"
#include "radio_capabilities.h"

// RF profile manager.
//
// A profile is a named, persisted description of how a radio interface has to be
// configured: which interface, in which role and mode, on which band, frequency,
// channel and bandwidth, at which requested TX power, and with which maximum TX
// power for the profile.
//
// The manager keeps three distinct values for the TX power:
//   - the configured value: what the user asked for
//   - the requested value: the configured value after the profile maximum is applied
//   - the effective value: the requested value clamped to the hardware and
//     regulatory limits of the selected interface
//
// The manager is hardware independent: it can be created, edited, saved, loaded and
// duplicated without any radio attached. Applying a profile needs the hardware.

#define RADIO_PROFILE_MAX_COUNT 16
#define RADIO_PROFILE_MAX_NAME_LENGTH 32
#define RADIO_PROFILE_FILE_VERSION 2

// Interface selectors
#define RADIO_PROFILE_IFACE_AUTO   -1   // pick the first usable radio interface
#define RADIO_PROFILE_IFACE_1      0
#define RADIO_PROFILE_IFACE_2      1
#define RADIO_PROFILE_IFACE_3      2
#define RADIO_PROFILE_IFACE_4      3
#define RADIO_PROFILE_IFACE_5      4

typedef struct
{
   char szName[RADIO_PROFILE_MAX_NAME_LENGTH+1];
   int iInterfaceIndex;         // radio interface index, or RADIO_PROFILE_IFACE_AUTO
   int iRole;                   // RADIO_CAP_ROLE_*
   int iMode;                   // RADIO_CAP_MODE_*
   int iBand;                   // RADIO_HW_SUPPORTED_BAND_*, or 0 for automatic
   u32 uFrequencyKhz;          // 0 for automatic
   int iChannel;                // 0 for automatic
   int iBandwidthMode;          // RADIO_CAP_BW_*
   int iConfiguredTxPowerDbm;   // what the user asked for
   int iProfileMaxTxPowerDbm;   // the maximum TX power allowed by this profile, 0 for no limit
   int iEnabled;                // 0 to keep the profile in the list but not apply it
} radio_profile_t;


// The resolved values of a profile, after it was checked against the
// capabilities of the selected radio interface.
typedef struct
{
   int iInterfaceIndex;         // the resolved radio interface index
   int iRole;                   // RADIO_CAP_ROLE_*
   int iMode;                   // RADIO_CAP_MODE_*
   int iBand;                   // RADIO_HW_SUPPORTED_BAND_*
   u32 uFrequencyKhz;          // the resolved frequency
   int iChannel;                // the resolved channel number
   int iBandwidthMode;          // RADIO_CAP_BW_*
   int iConfiguredTxPowerDbm;   // what the user asked for
   int iRequestedTxPowerDbm;    // the configured power, limited by the profile maximum
   int iEffectiveTxPowerDbm;    // the requested power, limited by the hardware/regulatory limit
   int iRawTxPower;             // the raw TX power index, as used by the hardware
   int iEnabled;
} radio_profile_resolved_t;


// Creates an empty profile list. Returns 1 on success.
int radio_profiles_init();

// Returns the number of stored profiles.
int radio_profiles_get_count();

// Returns a profile by index, or NULL.
const radio_profile_t* radio_profiles_get(int iIndex);

// Adds a new profile. Returns the new index, or -1 on failure.
int radio_profiles_add(const radio_profile_t* pProfile);

// Updates an existing profile. Returns 1 on success, 0 on failure.
int radio_profiles_update(int iIndex, const radio_profile_t* pProfile);

// Removes a profile. Returns 1 on success, 0 on failure.
int radio_profiles_remove(int iIndex);

// Duplicates a profile. Returns the new index, or -1 on failure.
int radio_profiles_duplicate(int iIndex, const char* szNewName);

// Finds a profile by name. Returns the index, or -1 if not found.
int radio_profiles_find_by_name(const char* szName);

// Loads the profiles from the configuration file. Returns the number of loaded profiles.
int radio_profiles_load();

// Saves the profiles to the configuration file. Returns 1 on success, 0 on failure.
int radio_profiles_save();

// Returns the path of the profiles configuration file.
const char* radio_profiles_get_file_path();

// Initializes a profile with sane defaults.
void radio_profiles_get_default_profile(radio_profile_t* pProfile);

// Validates a profile against the capabilities of the selected interface.
// On success returns RADIO_CAP_VALID and stores the effective values.
// On failure returns one of the RADIO_CAP_ERR_* codes.
int radio_profiles_validate(const radio_profile_t* pProfile, int* piEffectiveTxPowerDbm, char* szError, int iErrorSize);

// Resolves all the automatic values of a profile: the interface, the band, the
// frequency, the channel and the three TX power values.
// On success returns RADIO_CAP_VALID and fills the resolved structure.
// On failure returns one of the RADIO_CAP_ERR_* codes.
int radio_profiles_resolve(const radio_profile_t* pProfile, radio_profile_resolved_t* pResolved, char* szError, int iErrorSize);

// Resolves the interface index of a profile: RADIO_PROFILE_IFACE_AUTO is replaced
// by the first usable radio interface. Returns -1 if no interface is usable.
int radio_profiles_resolve_interface(const radio_profile_t* pProfile);

// Applies a profile to the hardware. Returns 1 on success, 0 on failure.
// On failure the reason is stored in the caller provided buffer.
int radio_profiles_apply(int iIndex, char* szError, int iErrorSize);

// Configures a radio interface with the resolved values of a profile: the
// monitor mode, the frequency, the bandwidth and the TX power. It does not
// save the profiles, it only applies the hardware configuration.
int radio_profiles_configure_interface(const radio_profile_resolved_t* pResolved, char* szError, int iErrorSize);

// Applies the RF profile that is bound to a radio interface, if there is one.
// It is used by the radio link opening path, so a stored profile takes
// effect when the link is opened and not only when the user asks for it from
// the user interface.
// The function returns:
//    1 - a profile was found and applied
//    0 - a profile was found but it could not be applied
//   -1 - there is no enabled profile bound to this interface
int radio_profiles_apply_to_interface(int iInterfaceIndex, char* szError, int iErrorSize);

// Returns the index of the profile that is stored in a menu item with the given
// index. The first item of the profile list is the section title, so the
// profiles start at the menu index 1 and the profile index is the menu index
// minus one. Returns -1 if the menu index is not a profile.
int radio_profiles_index_from_menu_index(int iMenuIndex, int iProfilesCount);

// Returns a human readable description of the role of a profile.
const char* radio_profiles_get_role_description(int iRole);

// Returns a human readable description of the mode of a profile.
const char* radio_profiles_get_mode_description(int iMode);

// Returns a human readable description of a band.
const char* radio_profiles_get_band_description(int iBand);

// Returns a human readable description of a bandwidth mode.
const char* radio_profiles_get_bandwidth_description(int iBandwidthMode);

// Returns the profile that is bound to a radio interface, or NULL if there is none.
// If more than one profile matches the interface, the first one is returned.
const radio_profile_t* radio_profiles_get_by_interface(int iInterfaceIndex);

// Returns the role (RADIO_CAP_ROLE_*) that is configured for a radio interface
// by the RF profiles, or -1 if no profile configures the interface.
int radio_profiles_get_role_for_interface(int iInterfaceIndex);

// Returns the mode (RADIO_CAP_MODE_*) that is configured for a radio interface
// by the RF profiles, or -1 if no profile configures the interface.
int radio_profiles_get_mode_for_interface(int iInterfaceIndex);

// Returns 1 if the role of a radio interface allows transmitting, 0 if it does not
// and -1 if the role is not configured by any profile (so the radio link
// configuration is the only one that decides).
int radio_profiles_role_allows_tx(int iInterfaceIndex);

// Returns 1 if the role of a radio interface allows receiving, 0 if it does not
// and -1 if the role is not configured by any profile.
int radio_profiles_role_allows_rx(int iInterfaceIndex);

// Returns 1 if the mode of a radio interface allows the station to transmit
// (the ground station and the search mode transmit, the vehicle mode receives),
// 0 if it does not and -1 if the mode is not configured by any profile.
int radio_profiles_mode_allows_tx(int iInterfaceIndex);
