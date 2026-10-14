#!/usr/bin/env bash
# Tests of push_symbols.sh against a local bare repository: the archive, the replacement on a re-run,
# the size limit, and two platforms' pushes racing.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

git init -q --bare "$work/symbols.git"
git clone -q "$work/symbols.git" "$work/seed"
(cd "$work/seed" && git -c user.name=t -c user.email=t@t commit -q --allow-empty -m init && git push -q origin HEAD)
# What an older release left: uncompressed files in the same folder.
(cd "$work/seed" && mkdir -p v1/linux-x86_64 && echo old > v1/linux-x86_64/torchlight.debug &&
 git add -A && git -c user.name=t -c user.email=t@t commit -q -m old && git push -q origin HEAD)

mkdir -p "$work/sym/sub"
head -c 300000 /dev/urandom > "$work/sym/torchlight.debug"
echo text > "$work/sym/sub/notes.txt"

"$here/push_symbols.sh" "$work/sym" v1 linux-x86_64 "file://$work/symbols.git" > /dev/null
git clone -q "$work/symbols.git" "$work/check"
files=$(cd "$work/check" && find v1 -type f | sort)
case "$files" in
  v1/linux-x86_64/symbols.tar.zst|v1/linux-x86_64/symbols.tar.xz) ;;
  *) fail "one archive replaces the old files: $files" ;;
esac
archive=$(ls "$work/check"/v1/linux-x86_64/symbols.tar.*)
mkdir "$work/out"
case "$archive" in
  *.zst) zstd -dq -c "$archive" | tar -xf - -C "$work/out" ;;
  *.xz) xz -dc "$archive" | tar -xf - -C "$work/out" ;;
esac
cmp -s "$work/sym/torchlight.debug" "$work/out/torchlight.debug" || fail "the archive holds the files"
[ -f "$work/out/sub/notes.txt" ] || fail "subfolders kept"

# A re-run of the same tag replaces it without failing.
echo more > "$work/sym/sub/notes.txt"
"$here/push_symbols.sh" "$work/sym" v1 linux-x86_64 "file://$work/symbols.git" > /dev/null || fail "a re-run"
[ "$(git -C "$work/symbols.git" rev-list --count HEAD)" = 4 ] || fail "a re-run adds one commit"

# Over the limit: it stops before cloning, with a message.
if SYMBOLS_LIMIT_MB=0 "$here/push_symbols.sh" "$work/sym" v1 macos-arm64 "$work/nowhere.git" 2> "$work/err"; then
  fail "over the limit fails"
fi
grep -q "over the 0 MB limit" "$work/err" || fail "the limit's message: $(cat "$work/err")"

# Two platforms at once: both land.
"$here/push_symbols.sh" "$work/sym" v2 windows-x86_64 "file://$work/symbols.git" > /dev/null &
"$here/push_symbols.sh" "$work/sym" v2 macos-arm64 "file://$work/symbols.git" > /dev/null &
wait %1 && wait %2 || fail "racing pushes"
git clone -q "$work/symbols.git" "$work/check2"
[ -d "$work/check2/v2/windows-x86_64" ] && [ -d "$work/check2/v2/macos-arm64" ] || fail "both platforms"
echo "test_push_symbols: ok"
