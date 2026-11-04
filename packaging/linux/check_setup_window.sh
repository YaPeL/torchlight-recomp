#!/bin/sh
# Starts an AppImage's first start and checks that its setup window opens, under a headless Wayland
# compositor (weston) and under an X server (Xvfb). CI runs it in its clean ubuntu:22.04 and
# archlinux containers (release.yml), after check_appimage.sh, which only resolves libraries:
# v1.0.0-rc1's setup window could not open under Wayland ("Window framebuffer support not
# available") and that check passed.
#
# "Opens" means: after the wait, the game is still running (waiting on the window's first page)
# and printed no "no window" line. A game that cannot open the window says so and stops, so a
# failure shows either way. Nothing of the machine's user is read or written: HOME, the XDG
# folders and the runtime folder are temporary, the image runs extracted there (its portable
# folder, beside it, too) and the run is stopped before any choice.
# Usage: check_setup_window.sh IMAGE.AppImage
set -eu
[ $# -eq 1 ] || { echo "usage: $0 IMAGE.AppImage" >&2; exit 2; }
image=$(readlink -f "$1")
wait_seconds=20
work=$(mktemp -d)
server=
game=
cleanup() {
  # The game is stopped at once (a failed start waits on its error box); the server is asked first.
  for pid in $game; do kill -9 "$pid" 2> /dev/null || true; done
  for pid in $server; do kill "$pid" 2> /dev/null || true; done
  sleep 1
  for pid in $server; do kill -9 "$pid" 2> /dev/null || true; done
  rm -rf "$work" || true  # the check's outcome stands even if something is still busy there
}
trap cleanup EXIT
cd "$work"
"$image" --appimage-extract > /dev/null
mkdir -p home config data cache run
chmod 700 run

# Runs the first start: $1 names the display in the messages, $2 is the other display's variable
# (unset for the run), the rest are the run's display variables.
check() {
  name=$1 other=$2
  shift 2
  log="$work/$name.log"
  env -u APPIMAGE -u "$other" "$@" HOME="$work/home" XDG_CONFIG_HOME="$work/config" \
    XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" XDG_RUNTIME_DIR="$work/run" \
    "$work/squashfs-root/AppRun" > "$log" 2>&1 &
  game=$!
  elapsed=0
  while [ "$elapsed" -lt "$wait_seconds" ]; do
    if ! kill -0 "$game" 2> /dev/null; then
      cat "$log"
      echo "check_setup_window: $name: the game stopped instead of showing the setup window" >&2
      exit 1
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  if grep -q 'no window' "$log"; then
    cat "$log"
    echo "check_setup_window: $name: no setup window" >&2
    exit 1
  fi
  kill -9 "$game"
  wait "$game" 2> /dev/null || true
  game=
  echo "check_setup_window: $name: the setup window opened"
}

# Wayland: weston's headless backend on a socket of its own.
XDG_RUNTIME_DIR="$work/run" weston --backend=headless-backend.so --socket=check-wayland \
  --idle-time=0 > "$work/weston.log" 2>&1 &
server=$!
elapsed=0
until [ -S "$work/run/check-wayland" ]; do
  [ "$elapsed" -lt 10 ] && kill -0 "$server" 2> /dev/null || {
    cat "$work/weston.log"; echo "check_setup_window: weston did not start" >&2; exit 2; }
  sleep 1
  elapsed=$((elapsed + 1))
done
check wayland DISPLAY WAYLAND_DISPLAY=check-wayland SDL_VIDEO_DRIVER=wayland
kill "$server"
wait "$server" 2> /dev/null || true

# X11: Xvfb on a display nobody else uses here.
Xvfb :87 -screen 0 1280x800x24 -nolisten tcp > "$work/xvfb.log" 2>&1 &
server=$!
sleep 2
kill -0 "$server" 2> /dev/null || {
  cat "$work/xvfb.log"; echo "check_setup_window: Xvfb did not start" >&2; exit 2; }
check x11 WAYLAND_DISPLAY DISPLAY=:87 SDL_VIDEO_DRIVER=x11
