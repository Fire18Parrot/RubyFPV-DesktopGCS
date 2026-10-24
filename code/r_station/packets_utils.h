#pragma once
#include "../base/base.h"
#include "../base/models.h"

void packet_utils_init();
void packet_utils_uninit();

int compute_packet_uplink_datarate(int iVehicleRadioLink, int iRadioInterface, type_radio_links_parameters* pRadioLinksParams, u8* pPacketData);

int send_packet_to_radio_interfaces(u8* pPacketData, int nPacketLength, int iSendToSingleRadioLink, int iRepeatCount, int iTraceSrouce);

// Returns 1 (true) if at least one radio interface of this controller can
// send data on some radio link: it is opened for sending and its hardware
// driver can send RF packets. When none can, the controller can only receive
// packets, and the Ruby packets that need an answer from the vehicle (like
// the pairing requests or the pings) can not be sent at all.
int controller_has_radio_interfaces_for_send();

int get_controller_radio_interface_index_for_radio_link(int iLocalRadioLinkId);
