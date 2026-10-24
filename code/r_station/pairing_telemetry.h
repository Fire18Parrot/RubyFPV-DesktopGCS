#pragma once

#include "../base/base.h"

// The decision on the pairing of a vehicle from its ruby telemetry packets.
//
// The Ruby pairing flow needs the controller to send radio packets to the
// vehicle: the vehicle pairs only after it received a Ruby pairing request
// from the controller and it answered with a Ruby pairing confirmation
// packet. The radio interfaces of the controller can all be receive-only
// (their drivers, like the in-kernel rtw88 Realtek drivers, can not inject
// raw RF frames). On these controllers the Ruby pairing flow can not even
// start, and the router keeps the vehicle in the "pairing in progress" state
// forever, while it does receive the video stream and the ruby telemetry
// packets of the vehicle.
//
// On a controller that can not send radio packets, the vehicle is paired
// from the ruby telemetry evidence instead: the telemetry announces the
// vehicle id, the software version of the vehicle and the radio links that
// the vehicle transmits on, exactly what a Ruby pairing confirmation packet
// announces.
//
// The ruby telemetry of a vehicle is only trusted as a pairing evidence if:
//   - the ruby telemetry packet of the vehicle is known and its header
//     passed all the known vehicle checks of the router, and
//   - the ruby telemetry is fresh (the vehicle is alive on the current radio
//     links), and
//   - the software version of the vehicle is known (a ruby-like software),
//     and
//   - the vehicle id in the telemetry is the vehicle id of the model that
//     the controller expects on its current radio frequencies.

// How old can the last ruby telemetry packet of a vehicle be, in
// milliseconds, for the vehicle to be considered still alive on the current
// radio links. The ruby telemetry packets of a vehicle are sent around every
// 100 ms on the current radio links of the vehicle.
#define PAIRING_TELEMETRY_MAX_FRESHNESS_MS 2500

// The result of pairing_telemetry_can_pair_vehicle()
// The pairing of the vehicle is accepted, from its ruby telemetry.
#define PAIRING_TELEMETRY_RESULT_PAIR_VEHICLE                      0
#define PAIRING_TELEMETRY_RESULT_EPAIRING_DONE                     1
#define PAIRING_TELEMETRY_RESULT_EPAIRING_NO_TELEMETRY             2
#define PAIRING_TELEMETRY_RESULT_EPAIRING_TELEMETRY_OLD            3
#define PAIRING_TELEMETRY_RESULT_EPAIRING_UNKNOWN_SOFTWARE         4
#define PAIRING_TELEMETRY_RESULT_EPAIRING_VEHICLE_MISMATCH         5

int pairing_telemetry_can_pair_vehicle(bool bIsPairingDone, u32 uVehicleIdExpected, u32 uTelemetryVehicleId, u8 uRubyVersionByte, u32 uTimeMsLastTelemetry, u32 uTimeNowMs);
