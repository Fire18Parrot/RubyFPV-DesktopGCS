#pragma once

#define DEFAULT_MAX_LOOP_TIME_MILISECONDS 30
#define DEFAULT_MAX_RX_LOOP_TIMEOUT_MILISECONDS 49
#define DEFAULT_MAX_RX_LOOP_TIMEOUT_MILISECONDS_VEHICLE 60

#define DEFAULT_DELAY_WIFI_CHANGE 60

#define TIMEOUT_TELEMETRY_LOST 2000 // miliseconds
#define TIMEOUT_FC_TELEMETRY_LOST 2000 // miliseconds
#define TIMEOUT_FC_MESSAGE 2500  // How long should the last message from FC should be send to controller. Send it for 2 seconds

#define TIMEOUT_RADIO_FRAMES_FLAGS_CHANGE_CONFIRMATION 5000

#define TIMEOUT_LINK_TO_CONTROLLER_LOST 3000

#define TIMEOUT_TEST_LINK_STATE_START 30000 // Station waits up to 30s (spanning multiple vehicle idle windows under video saturation) for a START confirmation before aborting the test flow
#define TIMEOUT_TEST_LINK_STATE_APPLY_RADIO_PARAMS 5000
// Station pings uplink for 20s: must span the vehicle's full internal TestLink cycle
// (its own Apply window up to 5s + its 4s ping phase) so the vehicle's replies,
// which can only start once it has finished reconfiguring and resumed its interfaces,
// still fall inside the station's listening window. The old 4s window closed before
// the vehicle even reached its listening state whenever its apply was slow.
#define TIMEOUT_TEST_LINK_STATE_PING 20000
#define TIMEOUT_TEST_LINK_STATE_END 5000
