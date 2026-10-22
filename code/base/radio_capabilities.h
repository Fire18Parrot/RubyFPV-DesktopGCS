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
#include "hardware_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Radio capability model.
//
// This module is hardware independent: everything that needs a real radio to answer
// is separated from everything that only needs data. The parsing helpers take the
// raw text produced by the `iw` tool and turn it into capability structures, so
// they can be exercised without any radio attached.
//
// The capability model mirrors what the wireless stack reports:
//   - the bands the phy supports,
//   - the channels of each band, with the per channel regulatory TX power limit,
//   - the channel widths (HT20 / HT40 / VHT80) each band can actually use.
// Nothing is assumed: a bandwidth is only reported as supported if the phy
// advertises it, and the TX power limit of a channel is the one reported for that
// channel, not a global constant.

#define RADIO_CAP_MAX_CHANNELS 64
#define RADIO_CAP_MAX_BANDS 8

// The size of the buffer used to read the output of the `iw` tool
#define RADIO_CAP_IW_OUTPUT_SIZE 65536

// Bandwidth modes, in the order used by the UI
#define RADIO_CAP_BW_20MHZ  0
#define RADIO_CAP_BW_40MHZ  1
#define RADIO_CAP_BW_80MHZ  2
#define RADIO_CAP_BW_COUNT  3

// Role of a radio interface inside a link
#define RADIO_CAP_ROLE_TX      0
#define RADIO_CAP_ROLE_RX      1
#define RADIO_CAP_ROLE_TXRX    2
#define RADIO_CAP_ROLE_COUNT   3

// Operating modes of a radio interface
#define RADIO_CAP_MODE_STATION 0
#define RADIO_CAP_MODE_VEHICLE 1
#define RADIO_CAP_MODE_SEARCH  2
#define RADIO_CAP_MODE_COUNT   3

// Result of a validation
#define RADIO_CAP_VALID          0
#define RADIO_CAP_ERR_NO_INTERFACE   -1
#define RADIO_CAP_ERR_UNSUPPORTED    -2
#define RADIO_CAP_ERR_BAND           -3
#define RADIO_CAP_ERR_FREQUENCY      -4
#define RADIO_CAP_ERR_CHANNEL        -5
#define RADIO_CAP_ERR_BANDWIDTH      -6
#define RADIO_CAP_ERR_TXPOWER        -7
#define RADIO_CAP_ERR_REGULATORY     -8
#define RADIO_CAP_ERR_INCOMPLETE     -9
#define RADIO_CAP_ERR_NOT_APPLIED    -10
#define RADIO_CAP_ERR_NO_IW          -11
#define RADIO_CAP_ERR_NO_MONITOR     -12
#define RADIO_CAP_ERR_NO_PERMISSION  -13
#define RADIO_CAP_ERR_ANTENNA        -14

// One channel as reported by the wireless stack
typedef struct
{
   int iChannelNumber;      // 802.11 channel number
   u32 uFrequencyKhz;      // center frequency, in kHz
   int iMaxTxPowerDbm;     // maximum transmit power for this channel, in dBm
   int bDisabled;          // 1 if the regulatory domain forbids this channel
   int bRadarDetection;    // 1 if the channel requires DFS radar detection
   int iBandIndex;         // index in the owning band
} radio_cap_channel_t;

// One band (2.4 / 5.8 / ...) as reported by the wireless stack
typedef struct
{
   int iBandIndex;         // RADIO_HW_SUPPORTED_BAND_*
   int iMaxTxPowerDbm;     // maximum transmit power for this band, in dBm
   int iChannelCount;
   int bSupports20MHz;     // 1 if the band can be used with a 20MHz channel
   int bSupports40MHz;     // 1 if the band can be used with a 40MHz channel
   int bSupports80MHz;     // 1 if the band can be used with an 80MHz channel
   radio_cap_channel_t Channels[RADIO_CAP_MAX_CHANNELS];
} radio_cap_band_t;

// Capabilities of one radio interface
typedef struct
{
   char szName[64];           // kernel interface name, ex: wlan1
   int iPhyIndex;             // phy index, -1 if unknown
   int bMonitorSupported;     // 1 if the driver can do monitor mode
   int bMonitorEnabled;       // 1 if the interface is currently in monitor mode
   int iBandCount;
   radio_cap_band_t Bands[RADIO_CAP_MAX_BANDS];
   int iMaxTxPowerDbm;        // maximum over all bands, in dBm
} radio_cap_interface_t;


// --- Parsing (hardware independent, works on the text produced by `iw`) ---

// Parses the output of `iw phy<N> info` and fills the capabilities of the phy.
// Returns the number of channels found, or -1 on a parse error.
int radio_cap_parse_iw_phy_info(const char* szInfo, radio_cap_interface_t* pOut);

// Parses the output of `iw dev` and returns the phy index of an interface.
// Returns -1 if the interface is not found.
int radio_cap_parse_iw_dev_info(const char* szInfo, const char* szInterfaceName, int* piMonitorEnabled);

// Returns the band index of a frequency, or 0 if it is not in any known band.
int radio_cap_get_band_index(u32 uFrequencyKhz);

// Returns 1 if the frequency is inside a known band.
int radio_cap_is_frequency_in_band(u32 uFrequencyKhz);

// Converts a bandwidth mode to the `iw` HT flag, ex: "HT20", "HT40+", "80MHz".
// Returns 0 on success, -1 for an invalid mode.
int radio_cap_bandwidth_to_ht_flag(int iBandwidthMode, char* szOut, int iOutSize);

// Returns the bandwidth in MHz for a bandwidth mode, or 0 for an invalid mode.
int radio_cap_bandwidth_to_mhz(int iBandwidthMode);

// --- Capability queries (hardware independent once the struct is filled) ---

// Returns the number of bands of an interface.
int radio_cap_interface_band_count(const radio_cap_interface_t* pCap);

// Builds the supported bands bit mask of an interface, in the format that is
// used by the radio_hw_info_t::supportedBands field. The band index of the
// capability model is already the value of the RADIO_HW_SUPPORTED_BAND_*
// constant, so it is not shifted again.
u32 radio_cap_interface_get_supported_bands_mask(const radio_cap_interface_t* pCap);

// Returns the band structure of a band index, or NULL.
const radio_cap_band_t* radio_cap_interface_get_band(const radio_cap_interface_t* pCap, int iBandIndex);

// Returns 1 if the interface supports the given band.
int radio_cap_interface_supports_band(const radio_cap_interface_t* pCap, int iBand);

// Returns the channel structure for a frequency, or NULL.
const radio_cap_channel_t* radio_cap_interface_find_channel(const radio_cap_interface_t* pCap, u32 uFrequencyKhz);

// Returns 1 if the interface supports the frequency.
int radio_cap_interface_supports_frequency(const radio_cap_interface_t* pCap, u32 uFrequencyKhz);

// Returns the maximum transmit power in dBm for a frequency, or -1 if unknown.
int radio_cap_interface_get_max_txpower_dbm(const radio_cap_interface_t* pCap, u32 uFrequencyKhz);

// Returns the maximum transmit power in dBm for a band, or -1 if unknown.
int radio_cap_interface_get_band_max_txpower_dbm(const radio_cap_interface_t* pCap, int iBand);

// Returns 1 if the interface supports the given bandwidth in the given band.
int radio_cap_interface_supports_bandwidth(const radio_cap_interface_t* pCap, int iBand, int iBandwidthMode);

// Builds a human readable description of the supported bands, in a static buffer.
const char* radio_cap_interface_get_bands_description(const radio_cap_interface_t* pCap);

// Builds a human readable description of the supported bandwidths of a band,
// in a static buffer.
const char* radio_cap_interface_get_bandwidths_description(const radio_cap_interface_t* pCap, int iBand);

// --- Antenna restriction (hardware safety) ---

// Some cards are fitted with 5 GHz-only antennas. Tuning such a card to the
// 2.4 GHz band can physically destroy it, so the 2.4 GHz band has to be
// refused before any frequency is applied to it.
//
// Returns 1 if the card model is known to have 5 GHz-only antennas, 0 if it
// is not restricted. An unknown card model is NOT restricted: an
// unrecognised card must never be blocked by mistake.
int radio_cap_card_model_is_5ghz_only(int iCardModel);

// Returns 1 if the card model is allowed to be tuned to the given frequency,
// 0 if the frequency is refused because of the antenna restriction.
// An unknown card model allows every frequency.
int radio_cap_card_model_allows_frequency(int iCardModel, u32 uFrequencyKhz);

// Returns a human readable description of the antenna restriction of a card
// model, in a static buffer. It is empty for a card model that is not
// restricted.
const char* radio_cap_get_antenna_restriction_description(int iCardModel);

// --- Validation ---

// Validates a requested configuration against the capabilities of an interface.
// On success returns RADIO_CAP_VALID and stores the effective values.
// On failure returns one of the RADIO_CAP_ERR_* codes and stores a message in
// the caller provided buffer.
int radio_cap_validate_request(const radio_cap_interface_t* pCap,
      int iBand, u32 uFrequencyKhz, int iChannel, int iBandwidthMode,
      int iRequestedTxPowerDbm, int iProfileMaxTxPowerDbm,
      int* piEffectiveTxPowerDbm, char* szError, int iErrorSize);

// Returns a human readable description of a validation error code.
const char* radio_cap_get_error_description(int iErrorCode);

// --- Live probing ---

// Probes the capabilities of one radio interface, using the `iw` tool.
// Returns 1 on success, 0 on failure. On failure the last error is available
// through radio_cap_get_last_error().
int radio_cap_probe_interface(int iRadioIndex, radio_cap_interface_t* pOut);

// Probes the current monitor mode state of one radio interface.
// Returns the phy index of the interface, or -1 on failure.
// piMonitorEnabled is set to 1 if the interface is in monitor mode.
int radio_cap_probe_monitor_state(int iRadioIndex, int* piMonitorEnabled);

// Sets the monitor mode of one radio interface.
// Returns 1 on success, 0 on failure. On failure the last error is available
// through radio_cap_get_last_error().
int radio_cap_set_monitor_mode(int iRadioIndex, int bEnabled);

// Decides if a radio control command succeeded. The decision is based on the
// result of the command runner and on the exit status of the command, and not
// only on a search in the output of the command, so a command that failed is
// never reported as a success.
//
// iCommandResult is the result of the command runner: 1 if the command ran
// and exited with the status 0, 0 if the command could not be started and -1
// if the command ran and exited with a non zero status.
// iExitStatus is the exit status of the command.
// szOutput is the raw output of the command.
//
// Returns 1 if the command succeeded, 0 if it failed.
//
// It is exposed so the decision can be tested without a radio attached and
// without the privileges that a real mode change needs.
int radio_cap_command_succeeded(int iCommandResult, int iExitStatus, const char* szOutput);

// Applies the Ruby specific monitor flags ('none' and 'fcsfail') of one radio
// interface. The flags make the radiotap headers carry the same information as
// on the embedded hardware. Not every driver accepts them.
// Returns 1 if at least one flag was applied, 0 if none of them was.
int radio_cap_set_monitor_flags(int iRadioIndex);

// Returns 1 if the `iw` tool is available on this system.
int radio_cap_is_iw_available();

// Returns the path of the `iw` tool, or an empty string.
const char* radio_cap_get_iw_path();

// Returns the last error produced by a probe, in a static buffer.
const char* radio_cap_get_last_error();

#ifdef __cplusplus
}
#endif
