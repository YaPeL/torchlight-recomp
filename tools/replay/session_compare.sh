#!/bin/bash
# Compares two replay builds on a live session recording: every sampled frame must be identical,
# and the time to replay up to a frame is reported for each. Used to validate changes that must not
# alter the image (resource handling, caching) on sessions with many dynamic buffers.
#
# Usage: tools/replay/session_compare.sh BASE_REPLAY NEW_REPLAY SESSION.tlses GAME_DATA_ROOT A-B/STEP
#                                        LAST [OUT]
#   A-B/STEP  frames compared (every STEP-th frame from A to B)
#   LAST      frame the timed replays stop at
#   OUT       work directory (default out/replay/session_compare)
set -u
base=$1 new=$2 session=$3 data=$4 frames=$5 last=$6 out=${7:-out/replay/session_compare}
for v in base new; do
  bin=$base; [ $v = new ] && bin=$new
  rm -rf "$out/$v"; mkdir -p "$out/$v"
  "$bin" --session "$session" --game_data_root "$data" --out "$out/$v" --session_frames "$frames" \
    > "$out/$v/frames.log" 2>&1 || { echo "$v: replay failed (see $out/$v/frames.log)"; exit 1; }
  start=$(date +%s.%N)
  "$bin" --session "$session" --game_data_root "$data" --out "$out/$v/timed" --session_frame "$last" \
    > "$out/$v/timed.log" 2>&1
  echo "$v: replay to frame $last in $(echo "$(date +%s.%N) - $start" | bc) s"
done
total=0 differing=0
for f in "$out"/base/frame_*.png; do
  total=$((total + 1))
  cmp -s "$f" "$out/new/$(basename "$f")" || { differing=$((differing + 1)); echo "differs: $(basename "$f")"; }
done
echo "frames compared: $total, differing: $differing"
[ "$differing" -eq 0 ] && [ "$total" -gt 0 ]
