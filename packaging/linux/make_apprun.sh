#!/bin/sh
# Writes the AppImage's AppRun for EXECUTABLE to standard output (AppRun.in). Only the game knows
# --appimage_path; the replay image of CI's package-check gets no such option.
# Usage: make_apprun.sh EXECUTABLE
set -eu
here=$(cd "$(dirname "$0")" && pwd)
options=
[ "$1" = torchlight ] && options='${APPIMAGE:+"--appimage_path=$APPIMAGE"}'
sed -e "s|@EXECUTABLE@|$1|" -e "s|@APP_OPTIONS@|$options|" "$here/AppRun.in"
