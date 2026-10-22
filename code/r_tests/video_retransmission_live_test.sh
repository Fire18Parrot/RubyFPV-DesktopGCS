#!/usr/bin/env bash
#
# The sustained video retransmission loop, with the real desktop player.
#
# It is the same loop as the one of video_retransmission_loop_test.sh, but it
# is repeated many times and the reader of the video FIFO is the **real
# desktop player** (ruby_player_desktop), not the test: the video that the
# production code writes is decoded and displayed by the player.
#
# In every iteration:
#   1. the station receives the video of the vehicle (the production radio RX
#      code, the capture file stands for the air) and one video packet is
#      lost on the way;
#   2. the production station code asks the vehicle for the lost packet and
#      puts the request on the air (the production radio TX code);
#   3. the **production vehicle code** answers the request, in a separate
#      process (the vehicle code and the station code can not be linked in the
#      same binary);
#   4. the answer completes the block and the video of that frame reaches the
#      desktop player.
#
# The test needs no radio hardware, no monitor mode, no root privileges and no
# network access. It needs the desktop player (it is the reader of the FIFO)
# and it takes over the screen for a few seconds, to capture what the player
# shows: the user is warned with a desktop notification first.
#
# Usage:
#   ./code/r_tests/video_retransmission_live_test.sh [iterations]
#
# The test binaries are built with:
#   RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline test_video_tx_resend

set -u

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RX_BIN="$REPO_DIR/test_video_rx_pipeline"
TX_BIN="$REPO_DIR/test_video_tx_resend"
PLAYER_BIN="$REPO_DIR/ruby_player_desktop"
WORK_DIR="/tmp/ruby/rtr_loop_live"

ITERATIONS=10
if [ $# -ge 1 ]; then
   ITERATIONS="$1"
fi

TX_CAPTURE="$WORK_DIR/vehicle_video.pcap"
REQUEST_CAPTURE="$WORK_DIR/station_request.pcap"
ANSWER_CAPTURE="$WORK_DIR/vehicle_answer.pcap"
PLAYER_LOG="$WORK_DIR/player.log"
SCREENSHOT="$WORK_DIR/player.png"

say()  { printf '%s\n' "$*"; }
info() { printf '  %s\n' "$*"; }
fail() { printf '  ERROR: %s\n' "$*" >&2; }

cleanup() {
   if [ -n "${PLAYER_PID:-}" ]; then
      kill "$PLAYER_PID" 2>/dev/null
      wait "$PLAYER_PID" 2>/dev/null
   fi
}
trap cleanup EXIT INT TERM

say "================================================================"
say " RubyFPV sustained video retransmission loop, with the desktop player"
say "================================================================"

cd "$REPO_DIR" || exit 1

for BIN in "$RX_BIN" "$TX_BIN" "$PLAYER_BIN"; do
   if [ ! -x "$BIN" ]; then
      fail "the binary $BIN was not found."
      info "Build it with: RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline test_video_tx_resend ruby_player_desktop"
      exit 1
   fi
done

mkdir -p "$WORK_DIR" || exit 1
rm -f "$TX_CAPTURE" "$REQUEST_CAPTURE" "$ANSWER_CAPTURE" "$SCREENSHOT"

# The user is warned before the screen is taken over.
if command -v notify-send >/dev/null 2>&1; then
   notify-send --app-name="RubyFPV agent" --icon=video-display --urgency=normal \
      "RubyFPV agent" "The video retransmission test is starting: the RubyFPV player window will be shown for a few seconds."
   sleep 2
fi

# --- 1. the vehicle sends the video -----------------------------------------
say ""
say "--- 1. The vehicle sends the video (the real vehicle code) ---"
"$TX_BIN" --transmit "$TX_CAPTURE" >"$WORK_DIR/1_transmit.log" 2>&1
TX_STATUS=$?
tail -n 3 "$WORK_DIR/1_transmit.log" | sed 's/^/  /'
if [ "$TX_STATUS" != "0" ]; then
   fail "the vehicle transmission failed (status: $TX_STATUS)."
   exit 1
fi

# --- 2. the desktop player --------------------------------------------------
say ""
say "--- 2. The desktop player starts (it is the reader of the video FIFO) ---"
"$PLAYER_BIN" -p -d >"$PLAYER_LOG" 2>&1 &
PLAYER_PID=$!
info "The player is running (pid: $PLAYER_PID)."
# The player has to open the FIFO for reading, otherwise the write end of the
# FIFO can not be opened and the test would have nothing to send.
sleep 3
if ! kill -0 "$PLAYER_PID" 2>/dev/null; then
   fail "the desktop player stopped. The log is in $PLAYER_LOG"
   tail -n 5 "$PLAYER_LOG" | sed 's/^/  /'
   exit 1
fi

# --- 3. the sustained loop --------------------------------------------------
say ""
say "--- 3. The sustained retransmission loop ($ITERATIONS iterations) ---"
"$RX_BIN" --rtr-loop-live "$TX_CAPTURE" "$REQUEST_CAPTURE" "$ANSWER_CAPTURE" "$TX_BIN" "$ITERATIONS" >"$WORK_DIR/live_loop.log" 2>&1
RX_STATUS=$?
grep -E "PASS|FAIL|ERROR|iteration|video that reached" "$WORK_DIR/live_loop.log" | sed 's/^/  /'
if [ "$RX_STATUS" != "0" ]; then
   fail "the sustained retransmission loop failed (status: $RX_STATUS)."
   info "The log is in $WORK_DIR/live_loop.log"
   exit 1
fi

# --- 4. what the player shows ----------------------------------------------
say ""
say "--- 4. What the player shows ---"
if command -v qdbus6 >/dev/null 2>&1 && command -v spectacle >/dev/null 2>&1; then
   cat > "$WORK_DIR/raise_player.js" <<'EOF'
var wins = workspace.windowList ? workspace.windowList() : [];
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (w.resourceClass == "ruby_player_desktop") {
        try { w.keepAbove = true; } catch (e) {}
        try { w.minimized = false; } catch (e) {}
        try { w.fullScreen = true; } catch (e) {}
    }
}
EOF
   cat > "$WORK_DIR/restore_player.js" <<'EOF'
var wins = workspace.windowList ? workspace.windowList() : [];
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (w.resourceClass == "ruby_player_desktop") {
        try { w.fullScreen = false; } catch (e) {}
        try { w.keepAbove = false; } catch (e) {}
    }
}
EOF
   qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$WORK_DIR/raise_player.js" >/dev/null 2>&1
   for i in $(seq 0 30); do
      qdbus6 org.kde.KWin /Scripting/Script$i org.kde.kwin.Script.run >/dev/null 2>&1
   done
   sleep 2
   spectacle -b -n -f -o "$SCREENSHOT" >/dev/null 2>&1
   qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$WORK_DIR/restore_player.js" >/dev/null 2>&1
   for i in $(seq 0 30); do
      qdbus6 org.kde.KWin /Scripting/Script$i org.kde.kwin.Script.run >/dev/null 2>&1
   done
   if [ -s "$SCREENSHOT" ]; then
      info "The screenshot of the player is in $SCREENSHOT"
   else
      info "The screenshot could not be taken (the player window was not found?)."
   fi
else
   info "qdbus6 or spectacle is not available: the screenshot was skipped."
fi

info "The player log is in $PLAYER_LOG"
grep -E "Received the first video stream data|Detected the video stream format|frames decoded" "$PLAYER_LOG" | tail -n 5 | sed 's/^/  /'

say ""
say "  The sustained retransmission loop works: in every iteration the station"
say "  lost a video packet, it asked the vehicle for it with the production"
say "  code, the vehicle answered it with the production code and the video"
say "  of the frame that had the lost packet reached the desktop player."
say "  The logs are in $WORK_DIR"
exit 0
