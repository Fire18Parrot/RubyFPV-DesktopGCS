#!/usr/bin/env bash
#
# The video retransmission loop test.
#
# It runs the whole loop of the video retransmissions with the production code
# of both ends:
#
#   1. the vehicle builds the video packets with the real vehicle code
#      (code/r_vehicle/video_tx_buffers.cpp) and the real radio TX code
#      (radio_build_new_raw_ieee_packet() and radio_write_raw_ieee_packet())
#      writes the RF frames to a capture file;
#   2. the station reads that capture file with the real radio RX code
#      (radio_open_interface_for_read_from_file(),
#      radio_process_wlan_data_in(), packet_process_and_check()), one of the
#      video packets is lost on the way, the real station code
#      (ProcessorRxVideo::periodicLoopProcessor()) builds the retransmission
#      request and the real radio TX code writes the request to a second
#      capture file;
#   3. the vehicle reads the request with the real radio RX code, the real
#      vehicle code (process_data_tx_video_command()) answers it and the real
#      radio TX code writes the answer to a third capture file;
#   4. the station reads the answer and the video of the frame that had the
#      lost packet has to reach the desktop player FIFO, byte for byte.
#
# The capture files stand for the air: they have the link type of a monitor
# mode capture (DLT_IEEE802_11_RADIO), the production filter accepts them
# and the production code parses them. Every packet of the loop is built,
# framed, filtered, parsed and CRC checked by the production code.
#
# The test needs no radio hardware, no monitor mode, no root privileges and
# no network access. The vehicle code and the station code can not be linked
# in the same binary (they have their own copies of the shared variables),
# so the loop runs in two processes and the capture files are the link
# between them.
#
# Usage:
#   ./code/r_tests/video_retransmission_loop_test.sh
#
# The test binaries are built with:
#   RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline test_video_tx_resend

set -u

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RX_BIN="$REPO_DIR/test_video_rx_pipeline"
TX_BIN="$REPO_DIR/test_video_tx_resend"
WORK_DIR="/tmp/ruby/rtr_loop"

# The video packet that is lost on the way to the station. It is a data
# packet of a full video block (a block with no EC packet), so the station
# can not reconstruct it locally: it has to ask the vehicle for it.
DROP_INDEX=5

TX_CAPTURE="$WORK_DIR/vehicle_video.pcap"
REQUEST_CAPTURE="$WORK_DIR/station_request.pcap"
ANSWER_CAPTURE="$WORK_DIR/vehicle_answer.pcap"

say()  { printf '%s\n' "$*"; }
info() { printf '  %s\n' "$*"; }
fail() { printf '  ERROR: %s\n' "$*" >&2; }

say "================================================================"
say " RubyFPV video retransmission loop test"
say "================================================================"

cd "$REPO_DIR" || exit 1

if [ ! -x "$RX_BIN" ] || [ ! -x "$TX_BIN" ]; then
   fail "the test binaries were not found."
   info "Build them with: RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline test_video_tx_resend"
   exit 1
fi

mkdir -p "$WORK_DIR" || exit 1
rm -f "$TX_CAPTURE" "$REQUEST_CAPTURE" "$ANSWER_CAPTURE"

# --- 1. the vehicle sends the video -----------------------------------------
say ""
say "--- 1. The vehicle sends the video (the real vehicle code) ---"
"$TX_BIN" --transmit "$TX_CAPTURE" >"$WORK_DIR/1_transmit.log" 2>&1
TX_STATUS=$?
tail -n 4 "$WORK_DIR/1_transmit.log" | sed 's/^/  /'
if [ "$TX_STATUS" != "0" ]; then
   fail "the vehicle transmission failed (status: $TX_STATUS)."
   info "The log is in $WORK_DIR/1_transmit.log"
   exit 1
fi

# --- 2. the station asks for the lost packet --------------------------------
say ""
say "--- 2. The station receives the video, loses a packet and asks for it ---"
"$RX_BIN" --rtr-loop "$TX_CAPTURE" "$REQUEST_CAPTURE" - "$DROP_INDEX" >"$WORK_DIR/2_request.log" 2>&1
RX_STATUS=$?
grep -E "PASS|FAIL|lost packet" "$WORK_DIR/2_request.log" | sed 's/^/  /'
if [ "$RX_STATUS" != "0" ]; then
   fail "the station did not build the retransmission request (status: $RX_STATUS)."
   info "The log is in $WORK_DIR/2_request.log"
   exit 1
fi

# --- 3. the vehicle answers -------------------------------------------------
say ""
say "--- 3. The vehicle answers the retransmission request ---"
"$TX_BIN" --answer "$REQUEST_CAPTURE" "$ANSWER_CAPTURE" >"$WORK_DIR/3_answer.log" 2>&1
TX_STATUS=$?
grep -E "\[ OK \]|\[FAIL\]|retransmission requests" "$WORK_DIR/3_answer.log" | sed 's/^/  /'
if [ "$TX_STATUS" != "0" ]; then
   fail "the vehicle did not answer the retransmission request (status: $TX_STATUS)."
   info "The log is in $WORK_DIR/3_answer.log"
   exit 1
fi

# --- 4. the station receives the answer and the video reaches the player ----
say ""
say "--- 4. The station receives the answer and the video reaches the player ---"
"$RX_BIN" --rtr-loop "$TX_CAPTURE" "$REQUEST_CAPTURE" "$ANSWER_CAPTURE" "$DROP_INDEX" >"$WORK_DIR/4_verify.log" 2>&1
RX_STATUS=$?
grep -E "PASS|FAIL|video that reached" "$WORK_DIR/4_verify.log" | sed 's/^/  /'
if [ "$RX_STATUS" != "0" ]; then
   fail "the video did not reach the desktop player (status: $RX_STATUS)."
   info "The log is in $WORK_DIR/4_verify.log"
   exit 1
fi

say ""
say "  The video retransmission loop works end to end: the station lost a"
say "  video packet, it asked the vehicle for it with the production code, the"
say "  vehicle answered it with the production code and the video of the frame"
say "  that had the lost packet reached the desktop player, byte for byte."
say "  The logs are in $WORK_DIR"
exit 0
