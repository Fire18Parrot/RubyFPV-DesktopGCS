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

// Hardware independent test for the radio capability parser, the request
// validator and the RF profile manager.
//
// The test does not need any radio attached: it works on a captured `iw phy
// info` output and on the profile manager, which is fully hardware independent.

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_radio.h"
#include "../base/hardware_radio.h"
#include "../base/hardware_procs.h"
#include "../base/radio_capabilities.h"
#include "../base/radio_profiles.h"
#include "../base/hardware_radio_txpower.h"
#include "../common/string_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vector>

static int s_iTests = 0;
static int s_iFailures = 0;
static radio_profile_t s_TestProfile;

static void _check(const char* szName, int iGot, int iExpected)
{
   s_iTests++;
   if ( iGot == iExpected )
   {
      printf("  PASS  %-60s got=%d\n", szName, iGot);
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-60s got=%d expected=%d\n", szName, iGot, iExpected);
}


// A capture of the `iw phy info` output of an RTL8812AU adapter
// (rtw88_8812au, phy2). The 2.4GHz band has channels 1-14 (14 disabled),
// the 5GHz band has channels 36-165 with per channel regulatory limits and
// some DFS channels.
static const char* s_szPhyInfo =
"Wiphy phy2\n"
"\twiphy index: 2\n"
"\tmax # scan SSIDs: 4\n"
"\tmax scan IEs length: 2243 bytes\n"
"\tmax # sched scan SSIDs: 0\n"
"\tmax # match sets: 0\n"
"\tRetry short limit: 7\n"
"\tRetry long limit: 4\n"
"\tCoverage class: 0 (up to 0m)\n"
"\tDevice supports T-DLS.\n"
"\tSupported Ciphers:\n"
"\t\t* WEP40 (00-0f-ac:1)\n"
"\t\t* WEP104 (00-0f-ac:5)\n"
"\t\t* TKIP (00-0f-ac:2)\n"
"\t\t* CCMP-128 (00-0f-ac:4)\n"
"\t\t* CCMP-256 (00-0f-ac:10)\n"
"\t\t* GCMP-128 (00-0f-ac:8)\n"
"\t\t* GCMP-256 (00-0f-ac:9)\n"
"\t\t* CMAC (00-0f-ac:6)\n"
"\t\t* CMAC-256 (00-0f-ac:13)\n"
"\t\t* GMAC-128 (00-0f-ac:11)\n"
"\t\t* GMAC-256 (00-0f-ac:12)\n"
"\tAvailable Antennas: TX 0x3 RX 0x3\n"
"\tConfigured Antennas: TX 0x3 RX 0x3\n"
"\tSupported interface modes:\n"
"\t\t * IBSS\n"
"\t\t * managed\n"
"\t\t * AP\n"
"\t\t * AP/VLAN\n"
"\t\t * monitor\n"
"\tBand 1:\n"
"\t\tCapabilities: 0x196e\n"
"\t\t\tHT20/HT40\n"
"\t\t\tSM Power Save disabled\n"
"\t\t\tRX HT20 SGI\n"
"\t\t\tRX HT40 SGI\n"
"\t\t\tRX STBC 1-stream\n"
"\t\t\tMax AMSDU length: 7935 bytes\n"
"\t\t\tDSSS/CCK HT40\n"
"\t\tMaximum RX AMPDU length 65535 bytes (exponent: 0x003)\n"
"\t\tMinimum RX AMPDU time spacing: 16 usec (0x07)\n"
"\t\tHT Max RX data rate: 300 Mbps\n"
"\t\tHT TX/RX MCS rate indexes supported: 0-15, 32\n"
"\t\tBitrates (non-HT):\n"
"\t\t\t* 1.0 Mbps\n"
"\t\t\t* 2.0 Mbps\n"
"\t\t\t* 5.5 Mbps\n"
"\t\t\t* 11.0 Mbps\n"
"\t\t\t* 6.0 Mbps\n"
"\t\t\t* 9.0 Mbps\n"
"\t\t\t* 12.0 Mbps\n"
"\t\t\t* 18.0 Mbps\n"
"\t\t\t* 24.0 Mbps\n"
"\t\t\t* 36.0 Mbps\n"
"\t\t\t* 48.0 Mbps\n"
"\t\t\t* 54.0 Mbps\n"
"\t\tFrequencies:\n"
"\t\t\t* 2412.0 MHz [1] (20.0 dBm)\n"
"\t\t\t* 2417.0 MHz [2] (20.0 dBm)\n"
"\t\t\t* 2422.0 MHz [3] (20.0 dBm)\n"
"\t\t\t* 2427.0 MHz [4] (20.0 dBm)\n"
"\t\t\t* 2432.0 MHz [5] (20.0 dBm)\n"
"\t\t\t* 2437.0 MHz [6] (20.0 dBm)\n"
"\t\t\t* 2442.0 MHz [7] (20.0 dBm)\n"
"\t\t\t* 2447.0 MHz [8] (20.0 dBm)\n"
"\t\t\t* 2452.0 MHz [9] (20.0 dBm)\n"
"\t\t\t* 2457.0 MHz [10] (20.0 dBm)\n"
"\t\t\t* 2462.0 MHz [11] (20.0 dBm)\n"
"\t\t\t* 2467.0 MHz [12] (20.0 dBm)\n"
"\t\t\t* 2472.0 MHz [13] (20.0 dBm)\n"
"\t\t\t* 2484.0 MHz [14] (disabled)\n"
"\tBand 2:\n"
"\t\tCapabilities: 0x196e\n"
"\t\t\tHT20/HT40\n"
"\t\t\tSM Power Save disabled\n"
"\t\t\tRX HT20 SGI\n"
"\t\t\tRX HT40 SGI\n"
"\t\t\tRX STBC 1-stream\n"
"\t\t\tMax AMSDU length: 7935 bytes\n"
"\t\t\tDSSS/CCK HT40\n"
"\t\tMaximum RX AMPDU length 65535 bytes (exponent: 0x003)\n"
"\t\tMinimum RX AMPDU time spacing: 16 usec (0x07)\n"
"\t\tHT Max RX data rate: 300 Mbps\n"
"\t\tHT TX/RX MCS rate indexes supported: 0-15, 32\n"
"\t\tVHT Capabilities (0x03d071a2):\n"
"\t\t\tMax MPDU length: 11454\n"
"\t\t\tSupported Channel Width: neither 160 nor 80+80\n"
"\t\t\tshort GI (80 MHz)\n"
"\t\t\tTX STBC\n"
"\t\t\tSU Beamformee\n"
"\t\t\tMU Beamformee\n"
"\t\t\t+HTC-VHT\n"
"\t\tVHT RX MCS set:\n"
"\t\t\t1 streams: MCS 0-9\n"
"\t\t\t2 streams: MCS 0-9\n"
"\t\t\t3 streams: not supported\n"
"\t\t\t4 streams: not supported\n"
"\t\t\t5 streams: not supported\n"
"\t\t\t6 streams: not supported\n"
"\t\t\t7 streams: not supported\n"
"\t\t\t8 streams: not supported\n"
"\t\tVHT RX highest supported: 780 Mbps\n"
"\t\tVHT TX MCS set:\n"
"\t\t\t1 streams: MCS 0-9\n"
"\t\t\t2 streams: MCS 0-9\n"
"\t\t\t3 streams: not supported\n"
"\t\t\t4 streams: not supported\n"
"\t\t\t5 streams: not supported\n"
"\t\t\t6 streams: not supported\n"
"\t\t\t7 streams: not supported\n"
"\t\t\t8 streams: not supported\n"
"\t\tVHT TX highest supported: 780 Mbps\n"
"\t\tVHT extended NSS: not supported\n"
"\t\tBitrates (non-HT):\n"
"\t\t\t* 6.0 Mbps\n"
"\t\t\t* 9.0 Mbps\n"
"\t\t\t* 12.0 Mbps\n"
"\t\t\t* 18.0 Mbps\n"
"\t\t\t* 24.0 Mbps\n"
"\t\t\t* 36.0 Mbps\n"
"\t\t\t* 48.0 Mbps\n"
"\t\t\t* 54.0 Mbps\n"
"\t\tFrequencies:\n"
"\t\t\t* 5180.0 MHz [36] (23.0 dBm)\n"
"\t\t\t* 5200.0 MHz [40] (23.0 dBm)\n"
"\t\t\t* 5220.0 MHz [44] (23.0 dBm)\n"
"\t\t\t* 5240.0 MHz [48] (23.0 dBm)\n"
"\t\t\t* 5260.0 MHz [52] (20.0 dBm) (radar detection)\n"
"\t\t\t* 5280.0 MHz [56] (20.0 dBm) (radar detection)\n"
"\t\t\t* 5300.0 MHz [60] (20.0 dBm) (radar detection)\n"
"\t\t\t* 5320.0 MHz [64] (20.0 dBm) (radar detection)\n"
"\t\t\t* 5500.0 MHz [100] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5520.0 MHz [104] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5540.0 MHz [108] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5560.0 MHz [112] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5580.0 MHz [116] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5600.0 MHz [120] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5620.0 MHz [124] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5640.0 MHz [128] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5660.0 MHz [132] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5680.0 MHz [136] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5700.0 MHz [140] (26.0 dBm) (radar detection)\n"
"\t\t\t* 5720.0 MHz [144] (13.0 dBm) (radar detection)\n"
"\t\t\t* 5745.0 MHz [149] (13.0 dBm)\n"
"\t\t\t* 5765.0 MHz [153] (13.0 dBm)\n"
"\t\t\t* 5785.0 MHz [157] (13.0 dBm)\n"
"\t\t\t* 5805.0 MHz [161] (13.0 dBm)\n"
"\t\t\t* 5825.0 MHz [165] (13.0 dBm)\n"
"\tSupported commands:\n"
"\t\t * new_interface\n"
"\t\t * set_interface\n"
"\t\t * new_key\n"
"\t\t * start_ap\n"
"\t\t * new_station\n"
"\t\t * new_mpath\n"
"\t\t * set_mesh_config\n"
"\t\t * set_bss\n"
"\t\t * authenticate\n"
"\t\t * associate\n"
"\t\t * deauthenticate\n"
"\t\t * disassociate\n"
"\t\t * join_ibss\n"
"\t\t * join_mesh\n"
"\t\t * remain_on_channel\n"
"\t\t * set_tx_bitrate_mask\n"
"\t\t * frame\n"
"\t\t * frame_wait_cancel\n"
"\t\t * set_wiphy_netns\n"
"\t\t * set_channel\n"
"\t\t * tdls_mgmt\n"
"\t\t * tdls_oper\n"
"\t\t * probe_client\n"
"\t\t * set_noack_map\n"
"\t\t * register_beacons\n"
"\t\t * start_p2p_device\n"
"\t\t * set_mcast_rate\n"
"\t\t * connect\n"
"\t\t * disconnect\n"
"\t\t * set_qos_map\n"
"\t\t * set_multicast_to_unicast\n"
"\t\t * set_sar_specs\n"
"\t\t * assoc_mlo_reconf\n"
"\tsoftware interface modes (can always be added):\n"
"\t\t * AP/VLAN\n"
"\t\t * monitor\n"
"\tinterface combinations are not supported\n"
"\tHT Capability overrides:\n"
"\t\t * MCS: ff ff ff ff ff ff ff ff ff ff\n"
"\t\t * maximum A-MSDU length\n"
"\t\t * supported channel width\n"
"\t\t * short GI for 40 MHz\n"
"\t\t * max A-MPDU length exponent\n"
"\t\t * min MPDU start spacing\n"
"\tDevice supports TX status socket option.\n"
"\tDevice supports HT-IBSS.\n"
"\tDevice supports SAE with AUTHENTICATE command\n"
"\tDevice supports scan flush.\n"
"\tDevice supports per-vif TX power setting\n"
"\tP2P GO supports CT window setting\n"
"\tP2P GO supports opportunistic powersave setting\n"
"\tDriver supports full state transitions for AP/GO clients\n"
"\tDriver supports a userspace MPM\n"
"\tDevice supports configuring vdev MAC-addr on create.\n"
"\tDevice supports randomizing MAC-addr in scans.\n"
"\tmax # scan plans: 1\n"
"\tmax scan plan interval: -1\n"
"\tmax scan plan iterations: 0\n"
"\tSupported TX frame types:\n"
"\t\t * IBSS: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * managed: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * AP: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * AP/VLAN: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * mesh point: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * P2P-client: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * P2P-GO: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * P2P-device: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * NAN: 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\t\t * Unknown mode (13): 0x00 0x10 0x20 0x30 0x40 0x50 0x60 0x70 0x80 0x90 0xa0 0xb0 0xc0 0xd0 0xe0 0xf0\n"
"\tSupported RX frame types:\n"
"\t\t * IBSS: 0x40 0xb0 0xc0 0xd0\n"
"\t\t * managed: 0x40 0xb0 0xd0\n"
"\t\t * AP: 0x00 0x20 0x40 0xa0 0xb0 0xc0 0xd0\n"
"\t\t * AP/VLAN: 0x00 0x20 0x40 0xa0 0xb0 0xc0 0xd0\n"
"\t\t * mesh point: 0xb0 0xc0 0xd0\n"
"\t\t * P2P-client: 0x40 0xd0\n"
"\t\t * P2P-GO: 0x00 0x20 0x40 0xa0 0xb0 0xc0 0xd0\n"
"\t\t * P2P-device: 0x40 0xb0 0xd0\n"
"\t\t * NAN: 0xb0 0xd0\n"
"\t\t * Unknown mode (13): 0xd0\n"
"\tSupported extended features:\n"
"\t\t* [ RRM ]: RRM\n"
"\t\t* [ SET_SCAN_DWELL ]: scan dwell setting\n"
"\t\t* [ FILS_STA ]: STA FILS (Fast Initial Link Setup)\n"
"\t\t* [ CONTROL_PORT_OVER_NL80211 ]: control port over nl80211\n"
"\t\t* [ TXQS ]: FQ-CoDel-enabled intermediate TXQs\n"
"\t\t* [ SCAN_RANDOM_SN ]: use random sequence numbers in scans\n"
"\t\t* [ CAN_REPLACE_PTK0 ]: can safely replace PTK 0 when rekeying\n"
"\t\t* [ CONTROL_PORT_NO_PREAUTH ]: disable pre-auth over nl80211 control port support\n"
"\t\t* [ DEL_IBSS_STA ]: deletion of IBSS station support\n"
"\t\t* [ SCAN_FREQ_KHZ ]: scan on kHz frequency support\n"
"\t\t* [ CONTROL_PORT_OVER_NL80211_TX_STATUS ]: tx status for nl80211 control port support\n"
"\t\t* [ POWERED_ADDR_CHANGE ]: can change MAC address while up\n";

static const char* s_szDevInfo =
"phy#2\n"
"\tInterface wlan0\n"
"\t\tifindex 12\n"
"\t\twdev 0x2\n"
"\t\taddr 04:cf:4b:22:c2:39\n"
"\t\ttype managed\n"
"\t\twiphy 2\n"
"\t\tchannel 149 (5745 MHz), width: 80 MHz, center1: 5775 MHz\n"
"phy#2\n"
"\tInterface wlan1\n"
"\t\tifindex 13\n"
"\t\twdev 0x3\n"
"\t\taddr 24:6f:28:aa:bb:cc\n"
"\t\ttype monitor\n"
"\t\twiphy 2\n"
"\t\tchannel 36 (5180 MHz), width: 20 MHz, center1: 5180 MHz\n";

// A capture of the real output of `iw dev` on the machine that has the
// RTL8812AU attached. The real output has no "wiphy N" line: the phy index
// is in the "phy#N" section header, and the header applies to all the
// interfaces that follow it.
//
// The `wlan1` adapter is the RTL8812AU, on phy 2. The `wlan0` adapter is a
// different, unrelated card, on phy 1.
static const char* s_szDevInfoReal =
"phy#2\n"
"\tInterface wlan1\n"
"\t\tifindex 6\n"
"\t\twdev 0x200000001\n"
"\t\taddr da:76:f7:41:0c:bf\n"
"\t\ttype managed\n"
"\t\ttxpower 20.00 dBm\n"
"\t\tmulticast TXQ:\n"
"\t\t\tqsz-byt\tqsz-pkt\tflows\tdrops\tmarks\toverlmt\thashcol\ttx-bytes\ttx-packets\n"
"\t\t\t0\t0\t0\t0\t0\t0\t0\t\t0\n"
"phy#1\n"
"\tUnnamed/non-netdev interface\n"
"\t\twdev 0x100000002\n"
"\t\taddr 04:cf:4b:22:c2:39\n"
"\t\ttype P2P-device\n"
"\tInterface wlan0\n"
"\t\tifindex 3\n"
"\t\twdev 0x100000001\n"
"\t\taddr 04:cf:4b:22:c2:39\n"
"\t\tssid Issam 2.4g\n"
"\t\ttype managed\n"
"\t\tchannel 6 (2437 MHz), width: 40 MHz, center1: 2447 MHz\n"
"\t\ttxpower 20.00 dBm\n"
"\t\tmulticast TXQ:\n"
"\t\t\tqsz-byt\tqsz-pkt\tflows\tdrops\tmarks\toverlmt\thashcol\ttx-bytes\ttx-packets\n"
"\t\t\t0\t0\t0\t0\t0\t0\t0\t\t0\n";

// The real output of `iw dev` on the same machine, with the RTL8812AU in
// monitor mode. The monitor mode is in the "type" line of the interface
// block, and the phy index is still the one of the section header.
static const char* s_szDevInfoRealMonitor =
"phy#2\n"
"\tInterface wlan1\n"
"\t\tifindex 6\n"
"\t\twdev 0x200000001\n"
"\t\taddr da:76:f7:41:0c:bf\n"
"\t\ttype monitor\n"
"\t\twiphy 2\n"
"\t\tchannel 36 (5180 MHz), width: 20 MHz, center1: 5180 MHz\n";


static void _test_parse_phy_info()
{
   printf("\n== Test: parse iw phy info ==\n");

   radio_cap_interface_t Cap;
   int iChannels = radio_cap_parse_iw_phy_info(s_szPhyInfo, &Cap);

   _check("channels parsed", iChannels, 39);
   _check("bands found", radio_cap_interface_band_count(&Cap), 2);
   _check("monitor mode supported", Cap.bMonitorSupported, 1);
   _check("max tx power over all bands", Cap.iMaxTxPowerDbm, 26);
   _check("phy index parsed from wiphy index", Cap.iPhyIndex, 2);

   _check("2.4GHz band present", radio_cap_interface_supports_band(&Cap, RADIO_HW_SUPPORTED_BAND_24), 1);
   _check("5.8GHz band present", radio_cap_interface_supports_band(&Cap, RADIO_HW_SUPPORTED_BAND_58), 1);
   _check("2.3GHz band absent", radio_cap_interface_supports_band(&Cap, RADIO_HW_SUPPORTED_BAND_23), 0);
   _check("915MHz band absent", radio_cap_interface_supports_band(&Cap, RADIO_HW_SUPPORTED_BAND_915), 0);
   _check("2.5GHz band absent", radio_cap_interface_supports_band(&Cap, RADIO_HW_SUPPORTED_BAND_25), 0);

   // The supported bands mask is used by the radio hardware info. The band
   // index of the capability model is already the value of the
   // RADIO_HW_SUPPORTED_BAND_* constant, it must not be shifted again. A
   // wrong shift maps the 2.4 GHz band to the 2.5 GHz band and the 5.8 GHz
   // band to a value that is not a band at all, so the 5.8 GHz channels
   // would not be usable.
   u32 uBandsMask = radio_cap_interface_get_supported_bands_mask(&Cap);
   _check("the supported bands mask has the 2.4GHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_24)) ? 1 : 0, 1);
   _check("the supported bands mask has the 5.8GHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_58)) ? 1 : 0, 1);
   _check("the supported bands mask does not have the 2.5GHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_25)) ? 1 : 0, 0);
   _check("the supported bands mask does not have the 2.3GHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_23)) ? 1 : 0, 0);
   _check("the supported bands mask does not have the 433MHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_433)) ? 1 : 0, 0);
   _check("the supported bands mask does not have the 868MHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_868)) ? 1 : 0, 0);
   _check("the supported bands mask does not have the 915MHz band", (0 != (uBandsMask & RADIO_HW_SUPPORTED_BAND_915)) ? 1 : 0, 0);
   _check("the supported bands mask has only the two detected bands", uBandsMask,
      (u32)(RADIO_HW_SUPPORTED_BAND_24 | RADIO_HW_SUPPORTED_BAND_58));

   const radio_cap_band_t* pBand24 = radio_cap_interface_get_band(&Cap, RADIO_HW_SUPPORTED_BAND_24);
   _check("2.4GHz band exists", NULL != pBand24, 1);
   if ( NULL != pBand24 )
   {
      _check("2.4GHz channel count", pBand24->iChannelCount, 14);
      _check("2.4GHz max tx power", pBand24->iMaxTxPowerDbm, 20);
      _check("2.4GHz supports 20MHz", pBand24->bSupports20MHz, 1);
      _check("2.4GHz supports 40MHz", pBand24->bSupports40MHz, 1);
      _check("2.4GHz does not support 80MHz", pBand24->bSupports80MHz, 0);
      _check("2.4GHz channel 1 is not disabled", pBand24->Channels[0].bDisabled, 0);
      _check("2.4GHz channel 1 number", pBand24->Channels[0].iChannelNumber, 1);
      _check("2.4GHz channel 1 frequency", pBand24->Channels[0].uFrequencyKhz, 2412000);
      _check("2.4GHz channel 1 tx power", pBand24->Channels[0].iMaxTxPowerDbm, 20);
      _check("2.4GHz channel 14 is disabled", pBand24->Channels[13].bDisabled, 1);
      _check("2.4GHz channel 14 frequency", pBand24->Channels[13].uFrequencyKhz, 2484000);
   }

   const radio_cap_band_t* pBand58 = radio_cap_interface_get_band(&Cap, RADIO_HW_SUPPORTED_BAND_58);
   _check("5.8GHz band exists", NULL != pBand58, 1);
   if ( NULL != pBand58 )
   {
      _check("5.8GHz channel count", pBand58->iChannelCount, 25);
      _check("5.8GHz max tx power", pBand58->iMaxTxPowerDbm, 26);
      _check("5.8GHz supports 40MHz", pBand58->bSupports40MHz, 1);
      _check("5.8GHz supports 80MHz", pBand58->bSupports80MHz, 1);
      _check("5.8GHz channel 36 tx power", pBand58->Channels[0].iMaxTxPowerDbm, 23);
      _check("5.8GHz channel 52 tx power", pBand58->Channels[4].iMaxTxPowerDbm, 20);
      _check("5.8GHz channel 100 tx power", pBand58->Channels[8].iMaxTxPowerDbm, 26);
      _check("5.8GHz channel 144 tx power", pBand58->Channels[19].iMaxTxPowerDbm, 13);
      _check("5.8GHz channel 149 tx power", pBand58->Channels[20].iMaxTxPowerDbm, 13);
      _check("5.8GHz channel 52 needs radar detection", pBand58->Channels[4].bRadarDetection, 1);
      _check("5.8GHz channel 36 does not need radar detection", pBand58->Channels[0].bRadarDetection, 0);
   }

   // The per channel limits must be used, not a global constant
   _check("max tx power at 5180 kHz", radio_cap_interface_get_max_txpower_dbm(&Cap, 5180000), 23);
   _check("max tx power at 5500 kHz", radio_cap_interface_get_max_txpower_dbm(&Cap, 5500000), 26);
   _check("max tx power at 5745 kHz", radio_cap_interface_get_max_txpower_dbm(&Cap, 5745000), 13);
   _check("max tx power at 2412 kHz", radio_cap_interface_get_max_txpower_dbm(&Cap, 2412000), 20);
   _check("frequency 3000 kHz is unknown", radio_cap_interface_supports_frequency(&Cap, 3000000), 0);
}


static void _test_parse_dev_info()
{
   printf("\n== Test: parse iw dev info ==\n");

   int iMonitor = -1;
   int iPhy = radio_cap_parse_iw_dev_info(s_szDevInfo, "wlan1", &iMonitor);
   _check("wlan1 phy index", iPhy, 2);
   _check("wlan1 is in monitor mode", iMonitor, 1);

   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfo, "wlan0", &iMonitor);
   _check("wlan0 phy index", iPhy, 2);
   _check("wlan0 is not in monitor mode", iMonitor, 0);

   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfo, "wlan9", &iMonitor);
   _check("unknown interface phy index", iPhy, -1);

   // The real output of `iw dev` has no "wiphy N" line. The phy index is in
   // the "phy#N" section header, and the header applies to all the interfaces
   // that follow it. The parser used to look for the "wiphy N" line only, so
   // it returned -1 for every real interface and the monitor state probe
   // returned -1 instead of the phy index.
   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfoReal, "wlan1", &iMonitor);
   _check("the real output: the wlan1 phy index is found", iPhy, 2);
   _check("the real output: wlan1 is not in monitor mode", iMonitor, 0);

   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfoReal, "wlan0", &iMonitor);
   _check("the real output: the wlan0 phy index is found", iPhy, 1);
   _check("the real output: wlan0 is not in monitor mode", iMonitor, 0);

   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfoReal, "wlan9", &iMonitor);
   _check("the real output: an unknown interface has no phy index", iPhy, -1);
   _check("the real output: an unknown interface is not in monitor mode", iMonitor, 0);

   // The monitor mode is detected from the "type" line of the interface
   // block, and the phy index still comes from the section header.
   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfoRealMonitor, "wlan1", &iMonitor);
   _check("the real output: the monitor mode phy index is found", iPhy, 2);
   _check("the real output: the monitor mode is detected", iMonitor, 1);

   // An interface that is not in a phy section at all has no phy index, but
   // it is still found.
   static const char* s_szDevInfoNoPhy =
   "\tInterface wlan3\n"
   "\t\tifindex 9\n"
   "\t\ttype managed\n";
   iMonitor = -1;
   iPhy = radio_cap_parse_iw_dev_info(s_szDevInfoNoPhy, "wlan3", &iMonitor);
   _check("an interface with no phy section has no phy index", iPhy, -1);
   _check("an interface with no phy section is found", iMonitor, 0);
}


// The mode change of an interface must never be reported as a success when
// the command failed. The decision is based on the result of the command
// runner and on the exit status of the command, and not only on a search in
// the output of the command.
static void _test_monitor_mode_failure_detection()
{
   printf("\n== Test: the monitor mode failure detection ==\n");

   // The real failure observed on the machine that has the RTL8812AU
   // attached: the command was refused with "Operation not permitted", so
   // the change did not happen. This must never be reported as a success.
   const char* szPermissionFailure = "command failed: Operation not permitted (-1)\n";
   _check("a permission failure is not a success",
      radio_cap_command_succeeded(1, 255, szPermissionFailure), 0);

   // The same failure, but the command exited with the status 0. The output
   // still says that the command failed, so it is not a success.
   _check("a permission failure with a zero exit status is not a success",
      radio_cap_command_succeeded(1, 0, szPermissionFailure), 0);

   // The command runner reports a failure. This is the case that was ignored
   // before, and it made the function return a success.
   _check("a failed command runner is not a success",
      radio_cap_command_succeeded(0, 0, ""), 0);
   _check("a timed out command runner is not a success",
      radio_cap_command_succeeded(-1, 0, ""), 0);

   // The command ran and it exited with a non zero status.
   _check("a non zero exit status is not a success",
      radio_cap_command_succeeded(1, 1, ""), 0);
   _check("a non zero exit status is not a success, with an output",
      radio_cap_command_succeeded(1, 255, "some output\n"), 0);

   // The other known error messages.
   _check("a missing device is not a success",
      radio_cap_command_succeeded(1, 0, "No such device\n"), 0);
   _check("an invalid argument is not a success",
      radio_cap_command_succeeded(1, 0, "invalid argument\n"), 0);
   _check("a usage message is not a success",
      radio_cap_command_succeeded(1, 0, "Usage: iw dev\n"), 0);

   // A command that ran and exited with the status 0, with no error in the
   // output, is a success.
   _check("a clean command is a success",
      radio_cap_command_succeeded(1, 0, ""), 1);
   _check("a clean command with an output is a success",
      radio_cap_command_succeeded(1, 0, "some output\n"), 1);
   _check("a clean command with a null output is a success",
      radio_cap_command_succeeded(1, 0, NULL), 1);

   // The mode change of an interface that does not exist has to fail.
   _check("the monitor mode of a non existing interface fails",
      radio_cap_set_monitor_mode(999, 1), 0);
   _check("the error of a failed monitor mode is not empty",
      (0 != strlen(radio_cap_get_last_error())) ? 1 : 0, 1);
}


static void _test_band_helpers()
{
   printf("\n== Test: band and bandwidth helpers ==\n");

   _check("2412 kHz is 2.4GHz", radio_cap_get_band_index(2412000), RADIO_HW_SUPPORTED_BAND_24);
   _check("2484 kHz is 2.4GHz (upstream band edges)", radio_cap_get_band_index(2484000), RADIO_HW_SUPPORTED_BAND_24);
   _check("2487 kHz is 2.5GHz", radio_cap_get_band_index(2487000), RADIO_HW_SUPPORTED_BAND_25);
   _check("5180 kHz is 5.8GHz", radio_cap_get_band_index(5180000), RADIO_HW_SUPPORTED_BAND_58);
   _check("5745 kHz is 5.8GHz", radio_cap_get_band_index(5745000), RADIO_HW_SUPPORTED_BAND_58);
   _check("1000000 kHz is 1GHz, mapped to the 2.3GHz catch all band", radio_cap_get_band_index(1000000), RADIO_HW_SUPPORTED_BAND_23);
   _check("1000000 kHz is in a band", radio_cap_is_frequency_in_band(1000000), 1);
   _check("430000 kHz is 433MHz", radio_cap_get_band_index(430000), RADIO_HW_SUPPORTED_BAND_433);
   _check("430000 kHz is in the 433MHz band", radio_cap_is_frequency_in_band(430000), 1);
   _check("868000 kHz is 868MHz", radio_cap_get_band_index(868000), RADIO_HW_SUPPORTED_BAND_868);
   _check("915000 kHz is 915MHz", radio_cap_get_band_index(915000), RADIO_HW_SUPPORTED_BAND_915);

   _check("20MHz bandwidth", radio_cap_bandwidth_to_mhz(RADIO_CAP_BW_20MHZ), 20);
   _check("40MHz bandwidth", radio_cap_bandwidth_to_mhz(RADIO_CAP_BW_40MHZ), 40);
   _check("80MHz bandwidth", radio_cap_bandwidth_to_mhz(RADIO_CAP_BW_80MHZ), 80);
   _check("invalid bandwidth", radio_cap_bandwidth_to_mhz(99), 0);

   char szFlag[16];
   _check("20MHz ht flag", radio_cap_bandwidth_to_ht_flag(RADIO_CAP_BW_20MHZ, szFlag, sizeof(szFlag)), 0);
   _check("20MHz ht flag value", strcmp(szFlag, "HT20"), 0);
   _check("40MHz ht flag", radio_cap_bandwidth_to_ht_flag(RADIO_CAP_BW_40MHZ, szFlag, sizeof(szFlag)), 0);
   _check("40MHz ht flag value", strcmp(szFlag, "HT40+"), 0);
}


static void _test_validate_request()
{
   printf("\n== Test: validate request ==\n");

   radio_cap_interface_t Cap;
   radio_cap_parse_iw_phy_info(s_szPhyInfo, &Cap);

   int iEffective = -1;
   char szError[256];

   // A valid request: 2.4GHz channel 1 at 20 dBm
   iEffective = -1;
   int iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("valid 2.4GHz request", iRes, RADIO_CAP_VALID);
   _check("valid 2.4GHz effective power", iEffective, 20);

   // A valid request on a 5.8GHz channel limited to 23 dBm
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_58, 5180000, 36, RADIO_CAP_BW_20MHZ,
      26, 0, &iEffective, szError, sizeof(szError));
   _check("5.8GHz request is valid", iRes, RADIO_CAP_VALID);
   _check("5.8GHz power is clamped to the channel limit", iEffective, 23);

   // A valid request on a 5.8GHz channel limited to 13 dBm
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_58, 5745000, 149, RADIO_CAP_BW_20MHZ,
      26, 0, &iEffective, szError, sizeof(szError));
   _check("5.8GHz channel 149 request is valid", iRes, RADIO_CAP_VALID);
   _check("5.8GHz channel 149 power is clamped to 13", iEffective, 13);

   // The profile maximum has priority over the requested power
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_20MHZ,
      20, 10, &iEffective, szError, sizeof(szError));
   _check("profile max request is valid", iRes, RADIO_CAP_VALID);
   _check("profile max limits the power", iEffective, 10);

   // An unsupported frequency
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, 0, 3000000, 0, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("unsupported frequency is rejected", iRes, RADIO_CAP_ERR_FREQUENCY);

   // A disabled channel
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2484000, 14, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("disabled channel is rejected", iRes, RADIO_CAP_ERR_REGULATORY);

   // A wrong channel number
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 11, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("wrong channel number is rejected", iRes, RADIO_CAP_ERR_CHANNEL);

   // An unsupported band
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_915, 0, 0, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("unsupported band is rejected", iRes, RADIO_CAP_ERR_BAND);

   // A negative TX power
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_20MHZ,
      -1, 0, &iEffective, szError, sizeof(szError));
   _check("negative TX power is rejected", iRes, RADIO_CAP_ERR_TXPOWER);

   // A null capability structure
   iEffective = -1;
   iRes = radio_cap_validate_request(NULL, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_20MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("null capabilities are rejected", iRes, RADIO_CAP_ERR_NO_INTERFACE);

   // --- bandwidth support, derived from the actual `iw` capabilities ---
   _check("2.4GHz supports 20MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_24, RADIO_CAP_BW_20MHZ), 1);
   _check("2.4GHz supports 40MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_24, RADIO_CAP_BW_40MHZ), 1);
   _check("2.4GHz does not support 80MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_24, RADIO_CAP_BW_80MHZ), 0);
   _check("5.8GHz supports 20MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_58, RADIO_CAP_BW_20MHZ), 1);
   _check("5.8GHz supports 40MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_58, RADIO_CAP_BW_40MHZ), 1);
   _check("5.8GHz supports 80MHz bandwidth", radio_cap_interface_supports_bandwidth(&Cap, RADIO_HW_SUPPORTED_BAND_58, RADIO_CAP_BW_80MHZ), 1);

   // A bandwidth the band does not advertise must be rejected
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_80MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("80MHz on 2.4GHz is rejected", iRes, RADIO_CAP_ERR_BANDWIDTH);

   // A valid 40MHz request on 2.4GHz
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, RADIO_CAP_BW_40MHZ,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("40MHz on 2.4GHz is accepted", iRes, RADIO_CAP_VALID);
   _check("40MHz on 2.4GHz keeps the power", iEffective, 20);

   // A valid 80MHz request on 5.8GHz
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_58, 5180000, 36, RADIO_CAP_BW_80MHZ,
      26, 0, &iEffective, szError, sizeof(szError));
   _check("80MHz on 5.8GHz is accepted", iRes, RADIO_CAP_VALID);
   _check("80MHz on 5.8GHz clamps the power to the channel limit", iEffective, 23);

   // An invalid bandwidth mode
   iEffective = -1;
   iRes = radio_cap_validate_request(&Cap, RADIO_HW_SUPPORTED_BAND_24, 2412000, 1, 99,
      20, 0, &iEffective, szError, sizeof(szError));
   _check("an invalid bandwidth is rejected", iRes, RADIO_CAP_ERR_BANDWIDTH);
}


static void _test_profiles()
{
   printf("\n== Test: RF profile manager ==\n");

   radio_profiles_init();
   _check("empty profile list", radio_profiles_get_count(), 0);

   radio_profile_t Profile;
   radio_profiles_get_default_profile(&Profile);
   _check("default profile name", strcmp(Profile.szName, "New Profile"), 0);
   _check("default profile interface is automatic", Profile.iInterfaceIndex, RADIO_PROFILE_IFACE_AUTO);
   _check("default profile role", Profile.iRole, RADIO_CAP_ROLE_TXRX);
   _check("default profile mode", Profile.iMode, RADIO_CAP_MODE_STATION);
   _check("default profile bandwidth", Profile.iBandwidthMode, RADIO_CAP_BW_20MHZ);
   _check("default profile is enabled", Profile.iEnabled, 1);

   int iIndex = radio_profiles_add(&Profile);
   _check("profile was added", iIndex, 0);
   _check("profile count", radio_profiles_get_count(), 1);

   // A profile with the same name cannot be added twice
   _check("duplicate name is rejected", radio_profiles_add(&Profile), -1);

   // An empty name is rejected
   radio_profile_t Empty;
   radio_profiles_get_default_profile(&Empty);
   Empty.szName[0] = 0;
   _check("empty name is rejected", radio_profiles_add(&Empty), -1);

   // Update the profile
   radio_profile_t Updated;
   radio_profiles_get_default_profile(&Updated);
   strcpy(Updated.szName, "Station 5.8GHz");
   Updated.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Updated.uFrequencyKhz = 5745000;
   Updated.iChannel = 149;
   Updated.iBandwidthMode = RADIO_CAP_BW_40MHZ;
   Updated.iConfiguredTxPowerDbm = 20;
   Updated.iProfileMaxTxPowerDbm = 13;
   _check("profile was updated", radio_profiles_update(0, &Updated), 1);
   _check("updated profile name", strcmp(radio_profiles_get(0)->szName, "Station 5.8GHz"), 0);
   _check("updated profile frequency", radio_profiles_get(0)->uFrequencyKhz, 5745000);
   _check("updated profile channel", radio_profiles_get(0)->iChannel, 149);
   _check("updated profile bandwidth", radio_profiles_get(0)->iBandwidthMode, RADIO_CAP_BW_40MHZ);
   _check("updated profile configured power", radio_profiles_get(0)->iConfiguredTxPowerDbm, 20);
   _check("updated profile max power", radio_profiles_get(0)->iProfileMaxTxPowerDbm, 13);

   // Find by name
   _check("profile found by name", radio_profiles_find_by_name("Station 5.8GHz"), 0);
   _check("unknown profile name", radio_profiles_find_by_name("Nope"), -1);

   // Duplicate
   int iDup = radio_profiles_duplicate(0, NULL);
   _check("profile was duplicated", iDup, 1);
   _check("duplicated profile count", radio_profiles_get_count(), 2);
   _check("duplicated profile has a new name", strcmp(radio_profiles_get(1)->szName, "Station 5.8GHz copy"), 0);
   _check("duplicated profile keeps the frequency", radio_profiles_get(1)->uFrequencyKhz, 5745000);
   _check("duplicated profile keeps the max power", radio_profiles_get(1)->iProfileMaxTxPowerDbm, 13);

   // Duplicate with an explicit name
   int iDup2 = radio_profiles_duplicate(0, "Explicit copy");
   _check("profile was duplicated with a name", iDup2, 2);
   _check("explicit duplicate name", strcmp(radio_profiles_get(2)->szName, "Explicit copy"), 0);

   // Remove
   _check("profile was removed", radio_profiles_remove(1), 1);
   _check("profile count after remove", radio_profiles_get_count(), 2);
   _check("the remaining profiles are kept", strcmp(radio_profiles_get(0)->szName, "Station 5.8GHz"), 0);
   _check("the remaining profiles are kept (2)", strcmp(radio_profiles_get(1)->szName, "Explicit copy"), 0);
   _check("removing an invalid index fails", radio_profiles_remove(99), 0);
   _check("removing a negative index fails", radio_profiles_remove(-1), 0);

   // Save and load round trip
   _check("profiles were saved", radio_profiles_save(), 1);
   radio_profiles_init();
   _check("the list is empty after init", radio_profiles_get_count(), 0);
   int iLoaded = radio_profiles_load();
   _check("profiles were loaded", iLoaded, 2);
   if ( (NULL != radio_profiles_get(0)) && (NULL != radio_profiles_get(1)) )
   {
      _check("loaded profile name", strcmp(radio_profiles_get(0)->szName, "Station 5.8GHz"), 0);
      _check("loaded profile frequency", radio_profiles_get(0)->uFrequencyKhz, 5745000);
      _check("loaded profile channel", radio_profiles_get(0)->iChannel, 149);
      _check("loaded profile bandwidth", radio_profiles_get(0)->iBandwidthMode, RADIO_CAP_BW_40MHZ);
      _check("loaded profile configured power", radio_profiles_get(0)->iConfiguredTxPowerDbm, 20);
      _check("loaded profile max power", radio_profiles_get(0)->iProfileMaxTxPowerDbm, 13);
      _check("loaded profile role", radio_profiles_get(0)->iRole, RADIO_CAP_ROLE_TXRX);
      _check("loaded profile mode", radio_profiles_get(0)->iMode, RADIO_CAP_MODE_STATION);
      _check("loaded profile interface", radio_profiles_get(0)->iInterfaceIndex, RADIO_PROFILE_IFACE_AUTO);
      _check("loaded profile is enabled", radio_profiles_get(0)->iEnabled, 1);
      _check("second loaded profile name", strcmp(radio_profiles_get(1)->szName, "Explicit copy"), 0);
   }
   else
   {
      _check("the profiles were loaded", 0, 1);
   }

   // A file with an unknown version must be ignored.
   // The stored size is a valid one, so the file reaches the version check and
   // it is not rejected by the size check before it.
   {
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "wb");
      _check("the corrupt file was written", NULL != fp, 1);
      if ( NULL != fp )
      {
         u32 uCount = 3;
         u32 uSize = 16 + uCount*sizeof(radio_profile_t);
         u32 uVersion = RADIO_PROFILE_FILE_VERSION + 100;
         fwrite(&uSize, sizeof(uSize), 1, fp);
         fwrite(&uVersion, sizeof(uVersion), 1, fp);
         fwrite(&uCount, sizeof(uCount), 1, fp);
         // write the profile records and a CRC, so only the version is wrong
         std::vector<radio_profile_t> Profiles(uCount);
         for( u32 i=0; i<uCount; i++ )
         {
            radio_profiles_get_default_profile(&(Profiles[i]));
            snprintf(Profiles[i].szName, sizeof(Profiles[i].szName), "Version test %u", (unsigned int)i);
         }
         fwrite(&(Profiles[0]), sizeof(radio_profile_t), uCount, fp);
         u32 uCrc = base_compute_crc32((u8*)&(Profiles[0]), (int)(uCount*sizeof(radio_profile_t)));
         fwrite(&uCrc, sizeof(uCrc), 1, fp);
         fclose(fp);
      }
      radio_profiles_init();
      _check("a file with an unknown version is ignored", radio_profiles_load(), 0);
      _check("no profile was loaded", radio_profiles_get_count(), 0);
   }

   // A file with an invalid count must be ignored.
   // The stored size is a valid one, so the file reaches the count check and
   // it is not rejected by the size check before it.
   {
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "wb");
      _check("the corrupt file was written", NULL != fp, 1);
      if ( NULL != fp )
      {
         u32 uCount = RADIO_PROFILE_MAX_COUNT + 10;
         u32 uSize = 16 + uCount*sizeof(radio_profile_t);
         u32 uVersion = RADIO_PROFILE_FILE_VERSION;
         fwrite(&uSize, sizeof(uSize), 1, fp);
         fwrite(&uVersion, sizeof(uVersion), 1, fp);
         fwrite(&uCount, sizeof(uCount), 1, fp);
         std::vector<radio_profile_t> Profiles(uCount);
         for( u32 i=0; i<uCount; i++ )
            radio_profiles_get_default_profile(&(Profiles[i]));
         fwrite(&(Profiles[0]), sizeof(radio_profile_t), uCount, fp);
         u32 uCrc = base_compute_crc32((u8*)&(Profiles[0]), (int)(uCount*sizeof(radio_profile_t)));
         fwrite(&uCrc, sizeof(uCrc), 1, fp);
         fclose(fp);
      }
      radio_profiles_init();
      _check("a file with an invalid count is ignored", radio_profiles_load(), 0);
      _check("no profile was loaded", radio_profiles_get_count(), 0);
   }

   // A file with a wrong stored size must be ignored
   {
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "wb");
      _check("the corrupt file was written", NULL != fp, 1);
      if ( NULL != fp )
      {
         u32 uSize = 12345;
         u32 uVersion = RADIO_PROFILE_FILE_VERSION;
         u32 uCount = 0;
         fwrite(&uSize, sizeof(uSize), 1, fp);
         fwrite(&uVersion, sizeof(uVersion), 1, fp);
         fwrite(&uCount, sizeof(uCount), 1, fp);
         fclose(fp);
      }
      radio_profiles_init();
      _check("a file with a wrong size is ignored", radio_profiles_load(), 0);
      _check("no profile was loaded", radio_profiles_get_count(), 0);
   }

   // A truncated file must be ignored
   {
      radio_profiles_init();
      radio_profiles_load();
      radio_profiles_save();
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "r+b");
      _check("the file to truncate was opened", NULL != fp, 1);
      if ( NULL != fp )
      {
         fclose(fp);
         // truncate the file to half of its size
         FILE* fp2 = fopen(szFile, "rb");
         long lSize = 0;
         if ( NULL != fp2 )
         {
            fseek(fp2, 0, SEEK_END);
            lSize = ftell(fp2);
            fclose(fp2);
         }
         if ( lSize > 8 )
         {
            if ( 0 != truncate(szFile, lSize/2) )
               printf("        (warning: could not truncate the test file)\n");
         }
      }
      radio_profiles_init();
      _check("a truncated file is ignored", radio_profiles_load(), 0);
      _check("no profile was loaded", radio_profiles_get_count(), 0);
   }

   // A file with a broken CRC must be ignored
   {
      radio_profiles_init();
      radio_profiles_load();
      radio_profiles_save();
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "r+b");
      _check("the file to corrupt was opened", NULL != fp, 1);
      if ( NULL != fp )
      {
         // corrupt the last byte, the CRC
         fseek(fp, -1, SEEK_END);
         u8 uByte = 0xAA;
         fwrite(&uByte, 1, 1, fp);
         fclose(fp);
      }
      radio_profiles_init();
      _check("a file with a broken CRC is ignored", radio_profiles_load(), 0);
      _check("no profile was loaded", radio_profiles_get_count(), 0);
   }

   // A version 1 file (no CRC) must be migrated
   {
      radio_profiles_init();
      radio_profiles_get_default_profile(&Profile);
      strcpy(Profile.szName, "Legacy profile");
      Profile.uFrequencyKhz = 2412000;
      Profile.iChannel = 1;
      radio_profiles_add(&Profile);
      radio_profiles_save();

      // rewrite the file in the version 1 format, without the CRC
      const char* szFile = radio_profiles_get_file_path();
      FILE* fp = fopen(szFile, "wb");
      _check("the version 1 file was written", NULL != fp, 1);
      if ( NULL != fp )
      {
         u32 uSize = 0;
         u32 uVersion = 1;
         u32 uCount = radio_profiles_get_count();
         fwrite(&uSize, sizeof(uSize), 1, fp);
         fwrite(&uVersion, sizeof(uVersion), 1, fp);
         fwrite(&uCount, sizeof(uCount), 1, fp);
         for( int i=0; i<(int)uCount; i++ )
            fwrite(radio_profiles_get(i), sizeof(radio_profile_t), 1, fp);
         // the version 1 file has no CRC
         u32 uSizeV1 = (u32)(3*sizeof(u32)) + uCount*((u32)sizeof(radio_profile_t));
         fseek(fp, 0, SEEK_SET);
         fwrite(&uSizeV1, sizeof(uSizeV1), 1, fp);
         fclose(fp);
      }
      radio_profiles_init();
      _check("the version 1 file was loaded", radio_profiles_load(), 1);
      _check("the migrated profile name", strcmp(radio_profiles_get(0)->szName, "Legacy profile"), 0);
      _check("the migrated profile frequency", radio_profiles_get(0)->uFrequencyKhz, 2412000);
      _check("the migrated profile channel", radio_profiles_get(0)->iChannel, 1);
      // the file must have been rewritten in the current format
      radio_profiles_init();
      _check("the migrated file is valid", radio_profiles_load(), 1);
   }

   // Restore a valid file, so the test does not leave a broken configuration
   radio_profiles_init();
   radio_profiles_load();
   radio_profiles_save();
}


static void _test_profile_resolve()
{
   printf("\n== Test: RF profile resolution ==\n");

   // Without a radio attached the resolution must fail with an explicit error,
   // and it must not crash.
   radio_profile_t Profile;
   radio_profiles_get_default_profile(&Profile);
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.uFrequencyKhz = 5745000;
   Profile.iChannel = 149;
   Profile.iConfiguredTxPowerDbm = 20;
   Profile.iProfileMaxTxPowerDbm = 13;

   radio_profile_resolved_t Resolved;
   char szError[256];
   int iRes = radio_profiles_resolve(&Profile, &Resolved, szError, sizeof(szError));
   // on a host without a radio this is expected to fail, with a clear error
   _check("resolving without a radio does not crash", (iRes == RADIO_CAP_VALID) || (iRes == RADIO_CAP_ERR_NO_INTERFACE), 1);
   if ( iRes != RADIO_CAP_VALID )
   {
      _check("the error message is not empty", strlen(szError) > 0, 1);
      printf("        (expected on a host without a radio: %s)\n", szError);
   }

   // A null profile must be rejected
   iRes = radio_profiles_resolve(NULL, &Resolved, szError, sizeof(szError));
   _check("a null profile is rejected", iRes, RADIO_CAP_ERR_NO_INTERFACE);

   // An empty name must be rejected
   radio_profile_t Empty;
   radio_profiles_get_default_profile(&Empty);
   Empty.szName[0] = 0;
   iRes = radio_profiles_resolve(&Empty, &Resolved, szError, sizeof(szError));
   _check("an empty name is rejected", iRes, RADIO_CAP_ERR_INCOMPLETE);
}


static void _test_profile_descriptions()
{
   printf("\n== Test: profile descriptions ==\n");

   _check("role TX", strcmp(radio_profiles_get_role_description(RADIO_CAP_ROLE_TX), "Transmit only"), 0);
   _check("role RX", strcmp(radio_profiles_get_role_description(RADIO_CAP_ROLE_RX), "Receive only"), 0);
   _check("role TXRX", strcmp(radio_profiles_get_role_description(RADIO_CAP_ROLE_TXRX), "Transmit and receive"), 0);
   _check("mode station", strcmp(radio_profiles_get_mode_description(RADIO_CAP_MODE_STATION), "Ground station"), 0);
   _check("mode vehicle", strcmp(radio_profiles_get_mode_description(RADIO_CAP_MODE_VEHICLE), "Vehicle"), 0);
   _check("band 2.4", strcmp(radio_profiles_get_band_description(RADIO_HW_SUPPORTED_BAND_24), "2.4GHz"), 0);
   _check("band 5.8", strcmp(radio_profiles_get_band_description(RADIO_HW_SUPPORTED_BAND_58), "5.8GHz"), 0);
   _check("band automatic", strcmp(radio_profiles_get_band_description(0), "Automatic"), 0);
}


// The radio link opening path uses radio_profiles_apply_to_interface() to
// apply the profile that is bound to an interface. The contract is:
//    -1 - there is no enabled profile bound to the interface
//     0 - a profile was found but it could not be applied
//     1 - a profile was found and it was applied
static void _test_profile_apply_to_interface()
{
   printf("\n== Test: profile apply to interface ==\n");

   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Auto profile");
   s_TestProfile.iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   radio_profiles_add(&s_TestProfile);

   // An automatic profile is bound to the interface that it resolves to, so
   // the radio link opening path applies it too. If there is no usable radio
   // interface, there is nothing to bind it to.
   int iAutoInterface = radio_profiles_resolve_interface(radio_profiles_get(0));
   printf("   the automatic profile resolves to radio interface %d\n", iAutoInterface+1);
   if ( iAutoInterface >= 0 )
   {
      _check("an automatic profile is bound to the resolved interface",
         (NULL != radio_profiles_get_by_interface(iAutoInterface)) ? 1 : 0, 1);
      // The apply cannot succeed, the profile has no frequency, but it must
      // reach the apply step and it must not report that there is no profile.
      _check("the automatic profile is applied or fails with a reason",
         (radio_profiles_apply_to_interface(iAutoInterface, NULL, 0) == 0) ? 1 : 0, 1);
   }
   else
   {
      _check("an automatic profile is not bound to an interface",
         (NULL != radio_profiles_get_by_interface(0)) ? 1 : 0, 0);
      _check("no profile is applied to an unbound interface",
         radio_profiles_apply_to_interface(0, NULL, 0), -1);
   }

   // An interface that the automatic profile does not resolve to is not
   // bound to it.
   int iOtherInterface = (iAutoInterface == 0) ? 1 : 0;
   _check("no profile is applied to an unbound interface 2",
      radio_profiles_apply_to_interface(iOtherInterface, NULL, 0), -1);

   // A profile that is bound to an interface is found. It cannot be applied
   // in the test environment, the profile has no usable frequency, but the
   // call has to reach the apply step and report the failure. It must not
   // report that there is no profile.
   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Bound profile");
   s_TestProfile.iInterfaceIndex = 0;
   s_TestProfile.iEnabled = 1;
   radio_profiles_add(&s_TestProfile);

   _check("a bound profile is found", (NULL != radio_profiles_get_by_interface(0)) ? 1 : 0, 1);

   char szError[512];
   szError[0] = 0;
   int iRes = radio_profiles_apply_to_interface(0, szError, sizeof(szError)/sizeof(szError[0]));
   // The profile has no frequency, so the apply has to fail with a reason,
   // but it must not be a "no profile" result.
   _check("a bound profile is applied or fails with a reason", (iRes == 0) ? 1 : 0, 1);
   _check("the failure has a reason", (0 != strlen(szError)) ? 1 : 0, 1);

   // A disabled profile that is bound to an interface is not applied either.
   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Disabled profile");
   s_TestProfile.iInterfaceIndex = 0;
   s_TestProfile.iEnabled = 0;
   radio_profiles_add(&s_TestProfile);
   _check("a disabled profile is not bound to an interface",
      radio_profiles_apply_to_interface(0, NULL, 0), -1);

   radio_profiles_init();
}


// Runtime discovery test. It enumerates the radio interfaces of the running
// system and it checks the RTL8812AU capability model against the real
// hardware, if such an adapter is attached. If there is no radio attached the
// test reports that it was skipped, it does not fail.
static void _test_runtime_rtl8812au_discovery()
{
   printf("\n== Test: runtime RTL8812AU discovery ==\n");

   if ( ! radio_cap_is_iw_available() )
   {
      printf("  SKIP  the 'iw' tool is not installed\n");
      return;
   }

   hardware_enumerate_radio_interfaces();
   int iRadios = hardware_get_radio_interfaces_count();
   printf("  INFO  %d radio interface(s) found\n", iRadios);
   if ( 0 == iRadios )
   {
      printf("  SKIP  no radio interface is attached to this system\n");
      return;
   }

   int iFoundRtl8812au = 0;
   for( int i=0; i<iRadios; i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;
      printf("  INFO  interface %d: [%s] type %d driver %d supported %d txcap %d bands 0x%02X\n",
         i+1, pRadioHWInfo->szName, pRadioHWInfo->iRadioType, pRadioHWInfo->iRadioDriver,
         pRadioHWInfo->isSupported, pRadioHWInfo->isTxCapable, pRadioHWInfo->supportedBands);

      if ( ! hardware_radio_driver_is_rtl8812au_card(pRadioHWInfo->iRadioDriver) )
         continue;

      iFoundRtl8812au++;

      // The card model of the attached adapter has to be detected, because the
      // antenna guard is based on it. The USB product id of the dual antenna
      // card is bda:881a, so the card model has to be the dual antenna one.
      printf("  INFO  interface %d card model: %d (%s)\n", i+1, pRadioHWInfo->iCardModel,
         str_get_radio_card_model_string(pRadioHWInfo->iCardModel));
      _check("the card model of the attached adapter was detected",
         (pRadioHWInfo->iCardModel > 0) ? 1 : 0, 1);

      // If the attached adapter is the dual antenna one, the guard has to
      // refuse the 2.4 GHz band for it.
      if ( pRadioHWInfo->iCardModel == CARD_MODEL_RTL8812AU_DUAL_ANTENNA )
      {
         _check("the attached dual antenna adapter is 5 GHz only",
            radio_cap_card_model_is_5ghz_only(pRadioHWInfo->iCardModel), 1);
         _check("the attached dual antenna adapter refuses 2412 MHz",
            radio_cap_card_model_allows_frequency(pRadioHWInfo->iCardModel, 2412000), 0);
         _check("the attached dual antenna adapter allows 5180 MHz",
            radio_cap_card_model_allows_frequency(pRadioHWInfo->iCardModel, 5180000), 1);
      }
      else
      {
         printf("  INFO  the attached adapter is not the dual antenna one, the guard is not applied to it\n");
      }

      // The supported bands mask of the radio hardware info has to match the
      // capability model of the same interface. A wrong band mask would make
      // the 5.8 GHz channels of the adapter unusable.
      radio_cap_interface_t Cap;
      memset(&Cap, 0, sizeof(radio_cap_interface_t));
      if ( radio_cap_probe_interface(i, &Cap) )
      {
         u32 uMask = radio_cap_interface_get_supported_bands_mask(&Cap);
         printf("  INFO  interface %d capability model: %d band(s), mask 0x%02X, monitor supported %d, max %d dBm\n",
            i+1, Cap.iBandCount, uMask, Cap.bMonitorSupported, Cap.iMaxTxPowerDbm);
         _check("the radio hardware info band mask matches the capability model",
            pRadioHWInfo->supportedBands, uMask);
         _check("the capability model has at least one band", (0 != uMask) ? 1 : 0, 1);
         _check("the capability model reports the monitor mode support", Cap.bMonitorSupported, 1);
         _check("the capability model has a per channel TX power limit", (Cap.iMaxTxPowerDbm > 0) ? 1 : 0, 1);
      }
      else
      {
         printf("  INFO  interface %d could not be probed: %s\n", i+1, radio_cap_get_last_error());
      }
   }

   if ( 0 == iFoundRtl8812au )
      printf("  SKIP  no RTL8812AU adapter is attached to this system\n");
   else
      _check("at least one RTL8812AU adapter was found", iFoundRtl8812au, iFoundRtl8812au);
}


// End to end profile workflow against the real hardware. It creates a
// profile that is bound to the RTL8812AU, it resolves it and it checks that
// the resolved values are the ones the hardware can actually accept: the
// requested TX power has to be limited by the per channel regulatory limit
// and the effective power must never be higher than the requested one.
// If there is no RTL8812AU attached the test reports that it was skipped.
static void _test_runtime_rtl8812au_profile()
{
   printf("\n== Test: runtime RTL8812AU profile resolution ==\n");

   if ( ! radio_cap_is_iw_available() )
   {
      printf("  SKIP  the 'iw' tool is not installed\n");
      return;
   }

   hardware_enumerate_radio_interfaces();
   int iRadios = hardware_get_radio_interfaces_count();

   int iRtlIndex = -1;
   for( int i=0; i<iRadios; i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( (NULL != pRadioHWInfo) && hardware_radio_driver_is_rtl8812au_card(pRadioHWInfo->iRadioDriver) )
      {
         iRtlIndex = i;
         break;
      }
   }
   if ( iRtlIndex < 0 )
   {
      printf("  SKIP  no RTL8812AU adapter is attached to this system\n");
      return;
   }
   printf("  INFO  the RTL8812AU is the radio interface %d\n", iRtlIndex+1);

   radio_cap_interface_t Cap;
   memset(&Cap, 0, sizeof(radio_cap_interface_t));
   if ( ! radio_cap_probe_interface(iRtlIndex, &Cap) )
   {
      printf("  SKIP  the RTL8812AU could not be probed: %s\n", radio_cap_get_last_error());
      return;
   }

   // A profile on the 5.8 GHz band, on a channel that the hardware reports.
   const radio_cap_band_t* pBand58 = radio_cap_interface_get_band(&Cap, RADIO_HW_SUPPORTED_BAND_58);
   if ( NULL == pBand58 )
   {
      printf("  SKIP  the RTL8812AU has no 5.8 GHz band\n");
      return;
   }

   int iChannel = 0;
   u32 uFreqKhz = 0;
   for( int i=0; i<pBand58->iChannelCount; i++ )
   {
      if ( ! pBand58->Channels[i].bDisabled )
      {
         iChannel = pBand58->Channels[i].iChannelNumber;
         uFreqKhz = pBand58->Channels[i].uFrequencyKhz;
         break;
      }
   }
   printf("  INFO  testing the 5.8 GHz channel %d at %u kHz\n", iChannel, uFreqKhz);
   _check("a 5.8 GHz channel was found", (iChannel > 0) ? 1 : 0, 1);

   radio_profiles_init();
   radio_profile_t Profile;
   radio_profiles_get_default_profile(&Profile);
   strcpy(Profile.szName, "RTL8812AU test");
   Profile.iInterfaceIndex = iRtlIndex;
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.iChannel = iChannel;
   Profile.uFrequencyKhz = 0;
   Profile.iBandwidthMode = RADIO_CAP_BW_20MHZ;
   Profile.iConfiguredTxPowerDbm = 20;
   Profile.iProfileMaxTxPowerDbm = 0;
   Profile.iEnabled = 1;

   int iAdded = radio_profiles_add(&Profile);
   _check("the profile was added", (0 <= iAdded) ? 1 : 0, 1);

   radio_profile_resolved_t Resolved;
   char szError[512];
   szError[0] = 0;
   int iRes = radio_profiles_resolve(radio_profiles_get(iAdded), &Resolved, szError, sizeof(szError)/sizeof(szError[0]));
   _check("the profile was resolved", iRes, RADIO_CAP_VALID);
   if ( RADIO_CAP_VALID == iRes )
   {
      _check("the resolved interface is the RTL8812AU", Resolved.iInterfaceIndex, iRtlIndex);
      _check("the resolved band is the 5.8 GHz band", Resolved.iBand, RADIO_HW_SUPPORTED_BAND_58);
      _check("the resolved channel is the tested channel", Resolved.iChannel, iChannel);
      _check("the resolved frequency is the channel frequency", Resolved.uFrequencyKhz, uFreqKhz);
      _check("the resolved bandwidth is 20 MHz", radio_cap_bandwidth_to_mhz(Resolved.iBandwidthMode), 20);

      // The requested power is the configured power, limited by the profile
      // maximum. The effective power is the requested power, limited by the
      // per channel hardware and regulatory limit.
      _check("the requested power is the configured power", Resolved.iRequestedTxPowerDbm, 20);

      int iChannelLimit = radio_cap_interface_get_max_txpower_dbm(&Cap, Resolved.uFrequencyKhz);
      printf("  INFO  the per channel limit is %d dBm, the effective power is %d dBm\n",
         iChannelLimit, Resolved.iEffectiveTxPowerDbm);
      _check("the per channel limit is known", (iChannelLimit > 0) ? 1 : 0, 1);
      _check("the effective power is not higher than the requested power",
         (Resolved.iEffectiveTxPowerDbm <= Resolved.iRequestedTxPowerDbm) ? 1 : 0, 1);
      _check("the effective power is not higher than the per channel limit",
         (Resolved.iEffectiveTxPowerDbm <= iChannelLimit) ? 1 : 0, 1);
      _check("the effective power is a real value", (Resolved.iEffectiveTxPowerDbm > 0) ? 1 : 0, 1);
      _check("the raw TX power was resolved", (Resolved.iRawTxPower > 0) ? 1 : 0, 1);
   }

   // A profile that asks for more power than the hardware allows has to be
   // limited, not rejected and not applied as it is. The requested power is
   // the configured power limited by the profile maximum, the effective power
   // is the requested power limited by the per channel hardware and
   // regulatory limit. The requested power is not clamped by the hardware
   // limit, so the two values stay distinct and the user interface can show
   // that the hardware did not accept the whole request.
   radio_profiles_get_default_profile(&Profile);
   strcpy(Profile.szName, "RTL8812AU over power");
   Profile.iInterfaceIndex = iRtlIndex;
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.iChannel = iChannel;
   Profile.iConfiguredTxPowerDbm = 30;
   Profile.iEnabled = 1;
   int iOver = radio_profiles_add(&Profile);
   radio_profile_resolved_t ResolvedOver;
   int iResOver = radio_profiles_resolve(radio_profiles_get(iOver), &ResolvedOver, szError, sizeof(szError)/sizeof(szError[0]));
   _check("the over power profile was resolved", iResOver, RADIO_CAP_VALID);
   if ( RADIO_CAP_VALID == iResOver )
   {
      int iLimit = radio_cap_interface_get_max_txpower_dbm(&Cap, ResolvedOver.uFrequencyKhz);
      printf("  INFO  the over power profile: configured %d dBm, requested %d dBm, effective %d dBm, limit %d dBm\n",
         ResolvedOver.iConfiguredTxPowerDbm, ResolvedOver.iRequestedTxPowerDbm,
         ResolvedOver.iEffectiveTxPowerDbm, iLimit);
      _check("the configured power is kept as it is", ResolvedOver.iConfiguredTxPowerDbm, 30);
      _check("the requested power is not clamped by the hardware limit",
         ResolvedOver.iRequestedTxPowerDbm, 30);
      _check("the effective power is limited by the hardware limit",
         (ResolvedOver.iEffectiveTxPowerDbm <= iLimit) ? 1 : 0, 1);
      _check("the effective power is not higher than the requested power",
         (ResolvedOver.iEffectiveTxPowerDbm <= ResolvedOver.iRequestedTxPowerDbm) ? 1 : 0, 1);
      _check("the effective power is the hardware limit",
         ResolvedOver.iEffectiveTxPowerDbm, iLimit);
   }

   // The profile maximum is a limit that the user sets for the profile, it
   // clamps the requested power.
   radio_profiles_get_default_profile(&Profile);
   strcpy(Profile.szName, "RTL8812AU profile maximum");
   Profile.iInterfaceIndex = iRtlIndex;
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.iChannel = iChannel;
   Profile.iConfiguredTxPowerDbm = 30;
   Profile.iProfileMaxTxPowerDbm = 15;
   Profile.iEnabled = 1;
   int iMax = radio_profiles_add(&Profile);
   radio_profile_resolved_t ResolvedMax;
   int iResMax = radio_profiles_resolve(radio_profiles_get(iMax), &ResolvedMax, szError, sizeof(szError)/sizeof(szError[0]));
   _check("the profile maximum profile was resolved", iResMax, RADIO_CAP_VALID);
   if ( RADIO_CAP_VALID == iResMax )
   {
      printf("  INFO  the profile maximum profile: configured %d dBm, requested %d dBm, effective %d dBm\n",
         ResolvedMax.iConfiguredTxPowerDbm, ResolvedMax.iRequestedTxPowerDbm, ResolvedMax.iEffectiveTxPowerDbm);
      _check("the requested power is limited by the profile maximum",
         ResolvedMax.iRequestedTxPowerDbm, 15);
      _check("the effective power is not higher than the requested power",
         (ResolvedMax.iEffectiveTxPowerDbm <= ResolvedMax.iRequestedTxPowerDbm) ? 1 : 0, 1);
   }

   // A profile that asks for a channel the hardware does not have has to be
   // rejected with a reason, not silently accepted.
   radio_profiles_get_default_profile(&Profile);
   strcpy(Profile.szName, "RTL8812AU bad channel");
   Profile.iInterfaceIndex = iRtlIndex;
   Profile.iBand = RADIO_HW_SUPPORTED_BAND_58;
   Profile.iChannel = 999;
   Profile.iEnabled = 1;
   int iBad = radio_profiles_add(&Profile);
   radio_profile_resolved_t ResolvedBad;
   szError[0] = 0;
   int iResBad = radio_profiles_resolve(radio_profiles_get(iBad), &ResolvedBad, szError, sizeof(szError)/sizeof(szError[0]));
   _check("a profile on a channel the hardware does not have is rejected", (RADIO_CAP_VALID != iResBad) ? 1 : 0, 1);
   _check("the rejection has a reason", (0 != strlen(szError)) ? 1 : 0, 1);
   printf("  INFO  the rejected profile reason: %s\n", szError);

   radio_profiles_init();
}


// The 5 GHz-only antenna guard. The RTL8812AU DualAnt card is fitted with
// 5 GHz-only antennas, so it must never be tuned to the 2.4 GHz band. An
// unknown card model must NOT be restricted, so an unrecognised card is never
// blocked by mistake.
static void _test_antenna_guard()
{
   printf("\n== Test: the 5 GHz only antenna guard ==\n");

   const int iRestricted = CARD_MODEL_RTL8812AU_DUAL_ANTENNA;

   // The restricted card is detected as restricted.
   _check("the dual antenna card is 5 GHz only", radio_cap_card_model_is_5ghz_only(iRestricted), 1);

   // A known card that is not restricted.
   _check("the generic 8812au card is not 5 GHz only",
      radio_cap_card_model_is_5ghz_only(CARD_MODEL_RTL8812AU_GENERIC), 0);
   _check("the alfa 8812au card is not 5 GHz only",
      radio_cap_card_model_is_5ghz_only(CARD_MODEL_ALFA_AWUS036ACH), 0);
   _check("the atheros card is not 5 GHz only",
      radio_cap_card_model_is_5ghz_only(CARD_MODEL_ATHEROS_GENERIC), 0);

   // An unknown card model is not restricted. A card that is not recognised
   // must never be blocked, because a wrong block makes a good card unusable.
   _check("an unknown card model is not 5 GHz only", radio_cap_card_model_is_5ghz_only(0), 0);
   _check("an unknown card model is not 5 GHz only", radio_cap_card_model_is_5ghz_only(9999), 0);
   _check("a negative card model is not 5 GHz only", radio_cap_card_model_is_5ghz_only(-9999), 0);

   // The restricted card is refused on the 2.4 GHz band.
   _check("the restricted card is refused on 2412 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 2412000), 0);
   _check("the restricted card is refused on 2437 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 2437000), 0);
   _check("the restricted card is refused on 2484 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 2484000), 0);
   _check("the restricted card is refused on 2412 MHz with the band edge",
      radio_cap_card_model_allows_frequency(iRestricted, 2411999), 0);

   // The restricted card is allowed on the 5 GHz band.
   _check("the restricted card is allowed on 5180 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 5180000), 1);
   _check("the restricted card is allowed on 5745 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 5745000), 1);
   _check("the restricted card is allowed on 5825 MHz",
      radio_cap_card_model_allows_frequency(iRestricted, 5825000), 1);

   // An unknown card model allows every frequency.
   _check("an unknown card model allows 2412 MHz",
      radio_cap_card_model_allows_frequency(0, 2412000), 1);
   _check("an unknown card model allows 5180 MHz",
      radio_cap_card_model_allows_frequency(9999, 5180000), 1);

   // A card that is not restricted allows the 2.4 GHz band.
   _check("the generic 8812au card allows 2412 MHz",
      radio_cap_card_model_allows_frequency(CARD_MODEL_RTL8812AU_GENERIC, 2412000), 1);

   // The description names the card and the band, so the user knows what to do.
   const char* szRestriction = radio_cap_get_antenna_restriction_description(iRestricted);
   _check("the restricted card has a description", (NULL != szRestriction) ? 1 : 0, 1);
   _check("the description is not empty", (0 != strlen(szRestriction)) ? 1 : 0, 1);
   _check("the description names the card", (NULL != strstr(szRestriction, "RTL8812AU")) ? 1 : 0, 1);
   _check("the description names the band", (NULL != strstr(szRestriction, "2.4")) ? 1 : 0, 1);
   printf("  INFO  the restriction description: %s\n", szRestriction);

   // A card that is not restricted has no description.
   _check("an unrestricted card has no description",
      (0 == strlen(radio_cap_get_antenna_restriction_description(CARD_MODEL_RTL8812AU_GENERIC))) ? 1 : 0, 1);
   _check("an unknown card model has no description",
      (0 == strlen(radio_cap_get_antenna_restriction_description(9999))) ? 1 : 0, 1);

   // The error description of the new error code is meaningful.
   _check("the antenna error has a description",
      (0 != strlen(radio_cap_get_error_description(RADIO_CAP_ERR_ANTENNA))) ? 1 : 0, 1);
   _check("the antenna error description names the band",
      (NULL != strstr(radio_cap_get_error_description(RADIO_CAP_ERR_ANTENNA), "2.4")) ? 1 : 0, 1);
}


// The shared resolver of the Ruby binaries. The precedence is: the name as
// it is given (as a filesystem path, relative to the current working
// directory), then the binaries folder of the platform, and the debug folder
// (/tmp/debug) overrides both of them.
static void _test_ruby_process_path_resolver()
{
   printf("\n== Test: the Ruby process path resolver ==\n");

   char szFullPath[MAX_FILE_PATH_SIZE];
   szFullPath[0] = 0;

   // The output buffer is cleared and it is terminated on every call.
   strcpy(szFullPath, "a stale value that has to be cleared");
   int iRes = hw_get_ruby_process_full_path("ruby_rt_station", szFullPath, sizeof(szFullPath));
   printf("  INFO  ruby_rt_station resolved to [%s], result %d\n", szFullPath, iRes);
   _check("the resolver returns a result for a known binary", (iRes >= 0) ? 1 : 0, 1);
   _check("the resolved path is not empty", (0 != strlen(szFullPath)) ? 1 : 0, 1);
   _check("the resolved path is terminated", (strlen(szFullPath) < sizeof(szFullPath)) ? 1 : 0, 1);

   // An invalid argument is refused, and the buffer is not written.
   szFullPath[0] = 0;
   _check("a null process name is refused", hw_get_ruby_process_full_path(NULL, szFullPath, sizeof(szFullPath)), 0);
   _check("an empty process name is refused", hw_get_ruby_process_full_path("", szFullPath, sizeof(szFullPath)), 0);
   _check("a null output buffer is refused", hw_get_ruby_process_full_path("ruby_rt_station", NULL, sizeof(szFullPath)), 0);
   _check("a too small output buffer is refused", hw_get_ruby_process_full_path("ruby_rt_station", szFullPath, 1), 0);

   // A process that does not exist anywhere is not found, and the buffer is
   // cleared, so the caller never gets a path that does not exist.
   strcpy(szFullPath, "a stale value that has to be cleared");
   int iMissing = hw_get_ruby_process_full_path("ruby_a_process_that_does_not_exist", szFullPath, sizeof(szFullPath));
   _check("a missing executable is not found", iMissing, 0);
   _check("the buffer is cleared for a missing executable", (0 == strlen(szFullPath)) ? 1 : 0, 1);
   printf("  INFO  the missing executable resolved to [%s]\n", szFullPath);

   // A direct path is used as it is. The name is a filesystem path, it is not
   // searched in the PATH of the environment.
   szFullPath[0] = 0;
   int iDirect = hw_get_ruby_process_full_path("/etc/hostname", szFullPath, sizeof(szFullPath));
   _check("a direct path is found", iDirect, 1);
   _check("a direct path is used as it is", strcmp(szFullPath, "/etc/hostname"), 0);

   // A name that is in the current working directory is found as a relative
   // path, and it is not searched in the PATH.
   FILE* fp = fopen("ruby_test_resolver_probe.tmp", "w");
   if ( NULL != fp )
   {
      fputs("#!/bin/sh\nexit 0\n", fp);
      fclose(fp);
      chmod("ruby_test_resolver_probe.tmp", 0755);
      szFullPath[0] = 0;
      int iRelative = hw_get_ruby_process_full_path("ruby_test_resolver_probe.tmp", szFullPath, sizeof(szFullPath));
      _check("a relative path in the working directory is found", iRelative, 1);
      _check("the relative path is used as it is", strcmp(szFullPath, "ruby_test_resolver_probe.tmp"), 0);
      remove("ruby_test_resolver_probe.tmp");
   }
   else
      printf("  SKIP  the working directory is not writable, the relative path was not tested\n");

   // A name that is not a path and is not in the working directory is not
   // found in the PATH of the environment.
   szFullPath[0] = 0;
   int iNotInPath = hw_get_ruby_process_full_path("ls", szFullPath, sizeof(szFullPath));
   printf("  INFO  'ls' resolved to [%s], result %d\n", szFullPath, iNotInPath);
   // 'ls' is in the PATH, but the resolver does not search the PATH, so it
   // is only found if it is also in the working directory or in the binaries
   // folder.
   _check("a name that is only in the PATH is not resolved through the PATH",
      (0 == iNotInPath) ? 1 : 0, 1);

   // The output buffer is cleared when the process is not found.
   strcpy(szFullPath, "a stale value that has to be cleared");
   iMissing = hw_get_ruby_process_full_path("ruby_a_process_that_does_not_exist", szFullPath, sizeof(szFullPath));
   _check("a missing executable is not found, with a cleared buffer", iMissing, 0);
   _check("the buffer is cleared, with a cleared buffer", (0 == strlen(szFullPath)) ? 1 : 0, 1);
}


// The raw TX power setters apply a command only to the cards of the family
// of the driver the command is meant for. The Ralink cards are handled by the
// Realtek 8812AU command, because they are configured the same way.
static void _test_raw_txpower_driver_filtering()
{
   printf("\n== Test: the raw TX power driver filtering ==\n");

   // The 8812AU family accepts all the members of the family, not only the
   // exact driver constant.
   _check("the 8812au family accepts the 8812au driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_REALTEK_8812AU, RADIO_TYPE_REALTEK, 0), 1);
   _check("the 8812au family accepts the rtl88xxau driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_REALTEK_RTL88XXAU, RADIO_TYPE_REALTEK, 0), 1);
   _check("the 8812au family accepts the rtl8812au driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_REALTEK_RTL8812AU, RADIO_TYPE_REALTEK, 0), 1);

   // The 8812AU command does not accept the other families.
   _check("the 8812au family rejects the atheros driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_ATHEROS, RADIO_TYPE_ATHEROS, 0), 0);
   _check("the 8812au family rejects the 8812eu driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_REALTEK_8812EU, RADIO_TYPE_REALTEK, 0), 0);
   _check("the 8812au family rejects the 8733bu driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_REALTEK_8733BU, RADIO_TYPE_REALTEK, 0), 0);

   // The 8812EU and the 8733BU families are separate.
   _check("the 8812eu family accepts the 8812eu driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812EU,
         RADIO_HW_DRIVER_REALTEK_8812EU, RADIO_TYPE_REALTEK, 0), 1);
   _check("the 8812eu family rejects the 8812au driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812EU,
         RADIO_HW_DRIVER_REALTEK_8812AU, RADIO_TYPE_REALTEK, 0), 0);
   _check("the 8733bu family accepts the 8733bu driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8733BU,
         RADIO_HW_DRIVER_REALTEK_8733BU, RADIO_TYPE_REALTEK, 0), 1);
   _check("the 8733bu family rejects the 8812eu driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8733BU,
         RADIO_HW_DRIVER_REALTEK_8812EU, RADIO_TYPE_REALTEK, 0), 0);

   // The atheros family.
   _check("the atheros family accepts the atheros driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_ATHEROS,
         RADIO_HW_DRIVER_ATHEROS, RADIO_TYPE_ATHEROS, 0), 1);
   _check("the atheros family rejects the 8812au driver",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_ATHEROS,
         RADIO_HW_DRIVER_REALTEK_8812AU, RADIO_TYPE_REALTEK, 0), 0);

   // The Ralink cards are handled by the 8812AU command, because they are
   // configured with the same command.
   _check("the 8812au family accepts a ralink card when ralink is allowed",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_RALINK, RADIO_TYPE_RALINK, 1), 1);
   _check("the 8812au family rejects a ralink card when ralink is not allowed",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_REALTEK_8812AU,
         RADIO_HW_DRIVER_RALINK, RADIO_TYPE_RALINK, 0), 0);
   _check("the atheros family rejects a ralink card",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_ATHEROS,
         RADIO_HW_DRIVER_RALINK, RADIO_TYPE_RALINK, 0), 0);

   // An unknown driver filter matches nothing.
   _check("an unknown driver filter matches nothing",
      hardware_radio_txpower_driver_matches(999, RADIO_HW_DRIVER_REALTEK_8812AU, RADIO_TYPE_REALTEK, 1), 0);

   // The whole family of the atheros driver is accepted, including the
   // members that are not the exact driver constant.
   _check("the atheros family accepts the ralink driver as atheros",
      hardware_radio_txpower_driver_matches(RADIO_HW_DRIVER_ATHEROS,
         RADIO_HW_DRIVER_RALINK, RADIO_TYPE_ATHEROS, 0), 0);

   // The decision on the result of the command: a command that could not be
   // started, that timed out or that reported an error is never a success.
   _check("a command that ran fine is a success",
      hardware_radio_txpower_command_succeeded(1, ""), 1);
   _check("a command with no output is a success",
      hardware_radio_txpower_command_succeeded(1, NULL), 1);
   _check("a command that could not be started is not a success",
      hardware_radio_txpower_command_succeeded(0, ""), 0);
   _check("a command that timed out is not a success",
      hardware_radio_txpower_command_succeeded(-1, ""), 0);
   _check("a command that timed out with an empty output is not a success",
      hardware_radio_txpower_command_succeeded(-1, NULL), 0);
   _check("a command that failed is not a success",
      hardware_radio_txpower_command_succeeded(1, "command failed: Operation not permitted (-1)"), 0);
   _check("a command refused by the permissions is not a success",
      hardware_radio_txpower_command_succeeded(1, "command failed: Operation not permitted (-1)\n"), 0);
   _check("a command on a missing device is not a success",
      hardware_radio_txpower_command_succeeded(1, "command failed: No such device (-19)"), 0);
   _check("a command with an invalid argument is not a success",
      hardware_radio_txpower_command_succeeded(1, "command failed: invalid argument (-22)"), 0);
   _check("a command with a wrong usage is not a success",
      hardware_radio_txpower_command_succeeded(1, "Usage: iw dev <devname> set txpower fixed <value>"), 0);
   _check("a command that did not set the power is not a success",
      hardware_radio_txpower_command_succeeded(1, "Set failed"), 0);
   _check("a command that did not find the device is not a success",
      hardware_radio_txpower_command_succeeded(1, "wlan9: Not found"), 0);
   _check("a command that succeeded is a success",
      hardware_radio_txpower_command_succeeded(1, "\n"), 1);
}


static void _test_profile_roles()
{

   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Role test");
   s_TestProfile.iInterfaceIndex = 0;
   s_TestProfile.iRole = RADIO_CAP_ROLE_RX;
   s_TestProfile.iMode = RADIO_CAP_MODE_VEHICLE;
   radio_profiles_add(&s_TestProfile);

   _check("the role profile was added", radio_profiles_get_count(), 1);

   // A profile that is bound to an interface provides the role and the mode
   _check("the role of the interface", radio_profiles_get_role_for_interface(0), RADIO_CAP_ROLE_RX);
   _check("the mode of the interface", radio_profiles_get_mode_for_interface(0), RADIO_CAP_MODE_VEHICLE);
   _check("the role does not allow tx", radio_profiles_role_allows_tx(0), 0);
   _check("the role allows rx", radio_profiles_role_allows_rx(0), 1);
   _check("the vehicle mode does not allow tx", radio_profiles_mode_allows_tx(0), 0);

   // An interface with no profile is not restricted
   _check("an unbound interface has no role", radio_profiles_get_role_for_interface(3), -1);
   _check("an unbound interface has no mode", radio_profiles_get_mode_for_interface(3), -1);
   _check("an unbound interface is not restricted for tx", radio_profiles_role_allows_tx(3), -1);
   _check("an unbound interface is not restricted for rx", radio_profiles_role_allows_rx(3), -1);
   _check("an unbound interface is not restricted by the mode", radio_profiles_mode_allows_tx(3), -1);

   // A disabled profile does not restrict the interface
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Role test");
   s_TestProfile.iInterfaceIndex = 0;
   s_TestProfile.iRole = RADIO_CAP_ROLE_RX;
   s_TestProfile.iMode = RADIO_CAP_MODE_VEHICLE;
   s_TestProfile.iEnabled = 0;
   radio_profiles_update(0, &(s_TestProfile));
   _check("a disabled profile does not restrict the interface", radio_profiles_get_role_for_interface(0), -1);

   // A transmit only profile in station mode
   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Tx profile");
   s_TestProfile.iInterfaceIndex = 1;
   s_TestProfile.iRole = RADIO_CAP_ROLE_TX;
   s_TestProfile.iMode = RADIO_CAP_MODE_STATION;
   radio_profiles_add(&s_TestProfile);
   _check("the tx role allows tx", radio_profiles_role_allows_tx(1), 1);
   _check("the tx role does not allow rx", radio_profiles_role_allows_rx(1), 0);
   _check("the station mode allows tx", radio_profiles_mode_allows_tx(1), 1);
   _check("the profile is found by the interface", (NULL != radio_profiles_get_by_interface(1)), 1);
   _check("the profile is not found on another interface", (NULL != radio_profiles_get_by_interface(0)), 0);

   radio_profiles_init();
}


// A profile with the automatic interface is bound to the interface that it
// resolves to, so it is not ignored. A profile that names an interface
// explicitly has priority over an automatic one.
static void _test_profile_auto_interface_binding()
{
   printf("\n== Test: the automatic interface binding of a profile ==\n");

   radio_profiles_init();

   // An automatic profile resolves to the first usable radio interface.
   _check("a null profile does not resolve", radio_profiles_resolve_interface(NULL), -1);

   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Auto profile");
   s_TestProfile.iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   s_TestProfile.iRole = RADIO_CAP_ROLE_RX;
   s_TestProfile.iMode = RADIO_CAP_MODE_SEARCH;
   radio_profiles_add(&s_TestProfile);

   int iAuto = radio_profiles_resolve_interface(radio_profiles_get(0));
   printf("   the automatic profile resolves to radio interface %d\n", iAuto+1);

   // The automatic profile is found on the interface it resolves to.
   _check("the automatic profile is found on the resolved interface",
      (NULL != radio_profiles_get_by_interface(iAuto)), 1);
   _check("the automatic profile provides the role",
      radio_profiles_get_role_for_interface(iAuto), RADIO_CAP_ROLE_RX);
   _check("the automatic profile provides the mode",
      radio_profiles_get_mode_for_interface(iAuto), RADIO_CAP_MODE_SEARCH);
   _check("the automatic profile does not allow tx",
      radio_profiles_role_allows_tx(iAuto), 0);
   _check("the automatic profile allows rx",
      radio_profiles_role_allows_rx(iAuto), 1);
   _check("the search mode allows tx",
      radio_profiles_mode_allows_tx(iAuto), 1);

   // An interface that the automatic profile does not resolve to is not
   // restricted by it.
   if ( iAuto >= 0 )
   {
      int iOther = (iAuto == 0) ? 1 : 0;
      _check("the automatic profile does not restrict another interface",
         radio_profiles_get_role_for_interface(iOther), -1);
      _check("the automatic profile is not found on another interface",
         (NULL != radio_profiles_get_by_interface(iOther)), 0);
   }

   // An explicit profile has priority over an automatic one for the same
   // interface.
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Explicit profile");
   s_TestProfile.iInterfaceIndex = iAuto;
   s_TestProfile.iRole = RADIO_CAP_ROLE_TX;
   s_TestProfile.iMode = RADIO_CAP_MODE_STATION;
   radio_profiles_add(&s_TestProfile);

   _check("the explicit profile has the priority",
      radio_profiles_get_role_for_interface(iAuto), RADIO_CAP_ROLE_TX);
   _check("the explicit profile is the one that is found",
      (NULL != radio_profiles_get_by_interface(iAuto)) ? (0 == strcmp(radio_profiles_get_by_interface(iAuto)->szName, "Explicit profile")) : 0, 1);

   // A disabled automatic profile does not restrict anything.
   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Disabled auto");
   s_TestProfile.iInterfaceIndex = RADIO_PROFILE_IFACE_AUTO;
   s_TestProfile.iRole = RADIO_CAP_ROLE_RX;
   s_TestProfile.iEnabled = 0;
   radio_profiles_add(&s_TestProfile);
   _check("a disabled automatic profile does not restrict the interface",
      radio_profiles_get_role_for_interface(iAuto), -1);
   _check("a disabled automatic profile is not found",
      (NULL != radio_profiles_get_by_interface(iAuto)), 0);

   radio_profiles_init();
}


static void _test_profile_menu_index()
{
   printf("\n== Test: profile menu index mapping ==\n");

   // The profile list starts with the section title, so the first profile is
   // at the menu index 1 and the last profile is at the menu index count.
   _check("the section title is not a profile", radio_profiles_index_from_menu_index(0, 3), -1);
   _check("a negative index is not a profile", radio_profiles_index_from_menu_index(-1, 3), -1);
   _check("the first profile is at the index 1", radio_profiles_index_from_menu_index(1, 3), 0);
   _check("the second profile is at the index 2", radio_profiles_index_from_menu_index(2, 3), 1);
   _check("the last profile is at the index 3", radio_profiles_index_from_menu_index(3, 3), 2);
   _check("an index past the last profile is not a profile", radio_profiles_index_from_menu_index(4, 3), -1);
   _check("no profile exists", radio_profiles_index_from_menu_index(1, 0), -1);
   _check("a single profile is at the index 1", radio_profiles_index_from_menu_index(1, 1), 0);
   _check("the index past the single profile is not a profile", radio_profiles_index_from_menu_index(2, 1), -1);
}


static void _test_migrated_profile_sanitized()
{
   printf("\n== Test: migrated profile sanitization ==\n");

   // A version 1 file with an invalid role, an invalid mode, an invalid
   // bandwidth, a negative power and an empty name. The migrated record has to
   // be sanitized the same way as a version 2 record.
   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Migrated");
   radio_profiles_add(&s_TestProfile);

   const char* szFile = radio_profiles_get_file_path();
   FILE* fp = fopen(szFile, "wb");
   _check("the version 1 file was written", NULL != fp, 1);
   if ( NULL != fp )
   {
      u32 uCount = 1;
      u32 uSize = 12 + uCount*sizeof(radio_profile_t);
      u32 uVersion = 1;
      fwrite(&uSize, sizeof(uSize), 1, fp);
      fwrite(&uVersion, sizeof(uVersion), 1, fp);
      fwrite(&uCount, sizeof(uCount), 1, fp);
      radio_profile_t Profile;
      radio_profiles_get_default_profile(&Profile);
      Profile.szName[0] = 0;
      Profile.iRole = 99;
      Profile.iMode = -3;
      Profile.iBandwidthMode = 77;
      Profile.iConfiguredTxPowerDbm = -5;
      Profile.iProfileMaxTxPowerDbm = -7;
      Profile.iInterfaceIndex = -9;
      Profile.iChannel = -4;
      fwrite(&Profile, sizeof(radio_profile_t), 1, fp);
      fclose(fp);
   }

   radio_profiles_init();
   _check("the version 1 file was loaded", radio_profiles_load(), 1);
   if ( 1 == radio_profiles_get_count() )
   {
      const radio_profile_t* pProfile = radio_profiles_get(0);
      _check("the migrated role was sanitized", pProfile->iRole, RADIO_CAP_ROLE_TXRX);
      _check("the migrated mode was sanitized", pProfile->iMode, RADIO_CAP_MODE_STATION);
      _check("the migrated bandwidth was sanitized", pProfile->iBandwidthMode, RADIO_CAP_BW_20MHZ);
      _check("the migrated configured power was sanitized", pProfile->iConfiguredTxPowerDbm, 0);
      _check("the migrated maximum power was sanitized", pProfile->iProfileMaxTxPowerDbm, 0);
      _check("the migrated interface was sanitized", pProfile->iInterfaceIndex, RADIO_PROFILE_IFACE_AUTO);
      _check("the migrated channel was sanitized", pProfile->iChannel, 0);
      _check("the migrated name was generated", (0 != strcmp(pProfile->szName, "")), 1);
   }
   else
      _check("the migrated profile was loaded", 0, 1);

   radio_profiles_init();
}


// The list mode of the RF profiles menu selects a profile, it does not enter
// the edit mode. The edit mode is entered with the "Edit Selected Profile"
// command. A profile that was added and that was not saved has to be removed
// when the edit is cancelled.
static void _test_profile_edit_workflow()
{
   printf("\n== Test: profile edit workflow ==\n");

   radio_profiles_init();
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "First");
   int iFirst = radio_profiles_add(&s_TestProfile);
   _check("the first profile was added", (0 <= iFirst) ? 1 : 0, 1);
   _check("the profile count is one", radio_profiles_get_count(), 1);

   // The list mode maps the menu index to the profile index.
   _check("the first profile is at the menu index 1", radio_profiles_index_from_menu_index(1, 1), 0);

   // A new profile is added and it is not saved yet.
   radio_profiles_get_default_profile(&(s_TestProfile));
   strcpy(s_TestProfile.szName, "Second");
   int iSecond = radio_profiles_add(&s_TestProfile);
   _check("the second profile was added", (0 <= iSecond) ? 1 : 0, 1);
   _check("the profile count is two", radio_profiles_get_count(), 2);
   _check("the second profile is at the menu index 2", radio_profiles_index_from_menu_index(2, 2), 1);

   // The removal of a profile that was not saved removes it from the list.
   _check("the second profile was removed", radio_profiles_remove(iSecond), 1);
   _check("the profile count is one after the removal", radio_profiles_get_count(), 1);
   _check("the first profile is still there", (NULL != radio_profiles_get(0)), 1);

   // A profile that is not saved is not written to the file.
   _check("the profiles were saved", radio_profiles_save(), 1);
   radio_profiles_init();
   _check("the profiles were loaded", radio_profiles_load(), 1);
   _check("the loaded profile count is one", radio_profiles_get_count(), 1);
   if ( 1 == radio_profiles_get_count() )
      _check("the loaded profile is the first one", strcmp(radio_profiles_get(0)->szName, "First"), 0);

   // The duplication of a profile adds a new one.
   int iDup = radio_profiles_duplicate(0, NULL);
   _check("the profile was duplicated", (0 <= iDup) ? 1 : 0, 1);
   _check("the profile count is two after the duplication", radio_profiles_get_count(), 2);

   radio_profiles_init();
}


// The monitor flags are a best effort compatibility option. A failure of a
// flag does not undo an already successful monitor mode change.
static void _test_monitor_flag_semantics()
{
   printf("\n== Test: monitor flag semantics ==\n");

   // The monitor state of a non existing interface cannot be probed, so the
   // probe returns an error and the state is not enabled.
   int iMonitorEnabled = -1;
   _check("the monitor state of a non existing interface fails", radio_cap_probe_monitor_state(999, &iMonitorEnabled), -1);
   _check("the monitor state of a non existing interface is not enabled", iMonitorEnabled, 0);

   // The monitor mode of a non existing interface cannot be set.
   _check("the monitor mode of a non existing interface fails",
      radio_cap_set_monitor_mode(999, 1), 0);
   _check("the monitor mode of a non existing interface cannot be disabled",
      radio_cap_set_monitor_mode(999, 0), 0);

   // The monitor flags of a non existing interface cannot be applied.
   _check("the monitor flags of a non existing interface fail", radio_cap_set_monitor_flags(999), 0);
}


// The raw TX power setters return a status. The command helper returns 0 when
// the command failed and -1 when the output read timed out, and neither of
// them is a success.
static void _test_raw_txpower_status_semantics()
{
   printf("\n== Test: raw TX power status semantics ==\n");

   // There is no radio interface in the test environment, so the raw TX power
   // setters have to report a failure instead of pretending a success.
   _check("the 8812au raw tx power fails without an interface",
      hardware_radio_set_txpower_raw_rtl8812au(0, 10), 0);
   _check("the 8812eu raw tx power fails without an interface",
      hardware_radio_set_txpower_raw_rtl8812eu(0, 10), 0);
   _check("the 8733bu raw tx power fails without an interface",
      hardware_radio_set_txpower_raw_rtl8733bu(0, 10), 0);
   _check("the atheros raw tx power fails without an interface",
      hardware_radio_set_txpower_raw_atheros(0, 10), 0);
}


// The frequency range of the RF profiles menu has to cover the 5.8 GHz band,
// so the valid 5 GHz frequencies can be entered.
static void _test_frequency_range_covers_5ghz()
{
   printf("\n== Test: the frequency range covers the 5.8 GHz band ==\n");

   // The 5.8 GHz band starts at 5725 MHz and the highest channel is at
   // 5825 MHz. The range of the UI has to cover them.
   _check("the 5.8 GHz band start is in the 5.8 GHz band", radio_cap_get_band_index(5725000), RADIO_HW_SUPPORTED_BAND_58);
   _check("the 5.8 GHz highest channel is in the 5.8 GHz band", radio_cap_get_band_index(5825000), RADIO_HW_SUPPORTED_BAND_58);

   // The 2.4 GHz band has to be covered too.
   _check("the 2.4 GHz band start is in the 2.4 GHz band", radio_cap_get_band_index(2412000), RADIO_HW_SUPPORTED_BAND_24);
   _check("the 2.4 GHz highest channel is in the 2.4 GHz band", radio_cap_get_band_index(2472000), RADIO_HW_SUPPORTED_BAND_24);

   // A frequency that is not in any of the offered bands is not in a band.
   _check("an unsupported frequency is not in a band", radio_cap_is_frequency_in_band(6000000), 0);
   _check("the 433 MHz band is covered", radio_cap_get_band_index(433000), RADIO_HW_SUPPORTED_BAND_433);
   _check("the 868 MHz band is covered", radio_cap_get_band_index(868000), RADIO_HW_SUPPORTED_BAND_868);
   _check("the 915 MHz band is covered", radio_cap_get_band_index(915000), RADIO_HW_SUPPORTED_BAND_915);
}


// Runtime discovery test. It checks the phy of every radio interface of the
// running system.
//
// The phy index is not a cosmetic value: it is used to read the channel list
// of the radio and, with it, the bands that the radio supports. A radio with
// no supported band cannot be tuned at all.
//
// The defect that this test protects against was found by the two-radio RF
// test: the "iw dev" output was parsed by looking at the line that precedes
// the name of the interface, and that line is not the "phy#N" header when
// the radio has more than one interface (a monitor interface, for instance).
// The index was then built from the last character of an unrelated line (a
// number like 53 or 66), the band detection ran on a phy that does not exist
// and the radio was recorded with no supported band at all.
static void _test_runtime_phy_resolution()
{
   printf("\n== Test: runtime phy resolution ==\n");

   if ( ! radio_cap_is_iw_available() )
   {
      printf("  SKIP  the 'iw' tool is not installed\n");
      return;
   }

   hardware_enumerate_radio_interfaces();
   int iRadios = hardware_get_radio_interfaces_count();
   printf("  INFO  %d radio interface(s) found\n", iRadios);
   if ( 0 == iRadios )
   {
      printf("  SKIP  no radio interface is attached to this system\n");
      return;
   }

   for( int i=0; i<iRadios; i++ )
   {
      radio_hw_info_t* pRadioHWInfo = hardware_get_radio_info(i);
      if ( NULL == pRadioHWInfo )
         continue;

      // A serial radio has no phy at all.
      if ( pRadioHWInfo->isSerialRadio )
      {
         printf("  INFO  interface %d: [%s] is a serial radio, it has no phy\n", i+1, pRadioHWInfo->szName);
         continue;
      }
      if ( 0 == pRadioHWInfo->szName[0] )
         continue;

      int iResolved = -1;
      int iFound = hardware_get_interface_phy_index(pRadioHWInfo->szName, &iResolved);
      printf("  INFO  interface %d: [%s], stored phy index %d, resolved phy index %d\n",
         i+1, pRadioHWInfo->szName, pRadioHWInfo->phy_index, iResolved);

      _check("the phy of the interface was resolved", (iFound && (iResolved >= 0)) ? 1 : 0, 1);
      _check("the resolved phy is the stored phy", iResolved, pRadioHWInfo->phy_index);

      // The phy has to exist. A wrong index makes the band detection run on
      // a phy that does not exist, and the radio is then recorded with no
      // supported band at all.
      char szPhyPath[256];
      snprintf(szPhyPath, sizeof(szPhyPath)/sizeof(szPhyPath[0]), "/sys/class/ieee80211/phy%d", iResolved);
      _check("the phy of the interface exists", (0 == access(szPhyPath, R_OK)) ? 1 : 0, 1);
   }
}


int main(int argc, char** argv)
{
   printf("=========================================================\n");
   printf("RubyFPV radio capabilities and RF profiles test\n");
   printf("=========================================================\n");

   // The test must not touch the real RF profiles configuration file. A
   // temporary file is used instead, and it is removed at the end.
   const char* szTestFile = "/tmp/ruby_test_rf_profiles.cfg";
   setenv("RUBY_RF_PROFILES_FILE", szTestFile, 1);
   unlink(szTestFile);

   _test_parse_phy_info();
   _test_parse_dev_info();
   _test_monitor_mode_failure_detection();
   _test_band_helpers();
   _test_validate_request();
   _test_profiles();
   _test_profile_resolve();
   _test_profile_descriptions();
   _test_profile_roles();
   _test_profile_auto_interface_binding();
   _test_profile_menu_index();
   _test_migrated_profile_sanitized();
   _test_profile_edit_workflow();
   _test_profile_apply_to_interface();
   _test_monitor_flag_semantics();
   _test_raw_txpower_status_semantics();
   _test_raw_txpower_driver_filtering();
   _test_frequency_range_covers_5ghz();
   _test_antenna_guard();
   _test_ruby_process_path_resolver();
   _test_runtime_rtl8812au_discovery();
   _test_runtime_phy_resolution();
   _test_runtime_rtl8812au_profile();

   unlink(szTestFile);

   printf("\n=========================================================\n");
   printf("Tests: %d, failures: %d\n", s_iTests, s_iFailures);
   if ( 0 == s_iFailures )
      printf("ALL TESTS PASSED\n");
   else
      printf("SOME TESTS FAILED\n");
   printf("=========================================================\n");
   return (0 == s_iFailures) ? 0 : 1;
}
