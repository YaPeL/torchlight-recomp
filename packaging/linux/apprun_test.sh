#!/bin/sh
# AppRun.in as make_apprun.sh writes it, over a fake executable that prints its arguments: the game
# gets the AppImage's path (spaces kept) before the player's options, nothing when it is not an
# AppImage; another executable never gets the option.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/usr/bin"
for name in torchlight replay; do
  printf '#!/bin/sh\nfor a in "$@"; do printf "[%%s]" "$a"; done\n' > "$work/usr/bin/$name"
  chmod +x "$work/usr/bin/$name"
done
check() {  # what, wanted, got
  [ "$2" = "$3" ] || { echo "FAIL: $1: wanted $2, got $3" >&2; exit 1; }
}
sh "$here/make_apprun.sh" torchlight > "$work/AppRun"
got=$(APPIMAGE="/my games/T L.AppImage" sh "$work/AppRun" --a "b c")
check "in an image" '[--appimage_path=/my games/T L.AppImage][--a][b c]' "$got"
got=$(env -u APPIMAGE sh "$work/AppRun" --a)
check "not in an image" '[--a]' "$got"
sh "$here/make_apprun.sh" replay > "$work/AppRun"
got=$(APPIMAGE=/x.AppImage sh "$work/AppRun" capture)
check "another executable" '[capture]' "$got"
echo "apprun_test: ok"
