#!/usr/bin/env bash
# Tests of store.sh's prune with fake keys: gh and git are replaced by functions over a fake store
# and fake branch checkouts, so nothing touches GitHub. Run by CI before the prune it guards
# (.github/workflows/ci.yml). Usage: tools/deps/test_store.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
export GITHUB_REPOSITORY=test/store
# shellcheck source=store.sh
source "$here/store.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

iso() {  # seconds ago -> ISO 8601 UTC (GNU date, else BSD date)
  local t=$(($(date -u +%s) - $1))
  date -u -d "@$t" +%Y-%m-%dT%H:%M:%SZ 2> /dev/null || date -u -r "$t" +%Y-%m-%dT%H:%M:%SZ
}

# A fake checkout of BRANCH whose key.sh knows the platforms given as PLATFORM=KEY; any other
# platform gets key.sh's usage error, as an older key.sh gives for macos.
fake_branch() {
  local branch=$1 pair
  shift
  mkdir -p "$work/branches/$branch/tools/deps" "$work/branches/$branch/tools/build-deps" \
    "$work/branches/$branch/patches"
  {
    echo 'case "${1:-linux}" in'
    for pair in "$@"; do echo "  ${pair%%=*}) echo ${pair#*=} ;;"; done
    echo '  *) echo "usage" >&2; exit 2 ;;'
    echo 'esac'
  } > "$work/branches/$branch/tools/deps/key.sh"
}

# The fake store: one asset per line, "NAME SECONDS_AGO".
fake_store() {
  local name ago id=1
  : > "$work/assets"
  : > "$work/deleted"
  while read -r name ago; do
    [ -n "$name" ] || continue
    printf '%s\t%s\t%s\n' "$id" "$name" "$(iso "$ago")" >> "$work/assets"
    id=$((id + 1))
  done
}

gh() {
  case "$*" in
    "api repos/$REPO/releases/tags/$STORE --jq"*) cut -f1-3 "$work/assets" ;;
    "api repos/$REPO/releases/tags/$STORE") return 0 ;;
    "api repos/$REPO/releases?per_page=100 --jq"*) return 0 ;;  # no old per-key releases
    "api repos/$REPO/git/matching-refs/tags/deps- --jq"*) return 0 ;;
    "api -X DELETE repos/$REPO/releases/assets/"*)
      local id=${4##*/}
      awk -F'\t' -v id="$id" '$1 == id { print $2 }' "$work/assets" >> "$work/deleted" ;;
    *) echo "unexpected gh $*" >&2; return 1 ;;
  esac
}

git() {
  case $1 in
    fetch) return 0 ;;
    archive)
      local ref=${2#origin/}
      tar -c -C "$work/branches/$ref" tools patches ;;
    show) echo "# a workflow on the store" ;;
    *) echo "unexpected git $*" >&2; return 1 ;;
  esac
}

expect_deleted() {  # CASE NAME...: exactly these assets deleted, in any order
  local case=$1 expected actual
  shift
  expected=$(printf '%s\n' "$@" | sed '/^$/d' | sort)
  actual=$(sort "$work/deleted")
  if [ "$expected" = "$actual" ]; then
    echo "ok   $case"
  else
    echo "FAIL $case"
    echo "  expected deleted:"; sed 's/^/    /' <<< "$expected"
    echo "  deleted:"; sed 's/^/    /' <<< "$actual"
    failures=$((failures + 1))
  fi
}

old=$((3 * 24 * 3600))  # past the grace period
recent=3600              # within it

# The stored archives, of every platform: the keys in use, old keys, an old key uploaded within
# the grace period, and names of no known platform.
store() {
  fake_store <<EOF
sdk-linux-amd64-1111111111111111.tar.zst $old
ogre-linux-amd64-1111111111111111.tar.zst $old
sdk-linux-amd64-2222222222222222.tar.zst $old
ogre-linux-amd64-2222222222222222.tar.zst $old
sdk-linux-amd64-0000000000000000.tar.zst $old
ogre-linux-amd64-0000000000000000.tar.zst $old
sdk-windows-amd64-3333333333333333.zip $old
ogre-windows-amd64-3333333333333333.zip $old
sdk-windows-amd64-4444444444444444.zip $old
ogre-windows-amd64-4444444444444444.zip $old
sdk-windows-amd64-0000000000000000.zip $old
ogre-windows-amd64-0000000000000000.zip $old
sdk-macos-arm64-5555555555555555.tar.zst $old
ogre-macos-arm64-5555555555555555.tar.zst $old
sdk-macos-arm64-0000000000000000.tar.zst $old
ogre-macos-arm64-0000000000000000.tar.zst $old
sdk-macos-arm64-6666666666666666.tar.zst $recent
ogre-macos-arm64-6666666666666666.tar.zst $recent
sdk-android-arm64-7777777777777777.tar.zst $old
notes.txt $old
EOF
}

# 1. main before the freeze that brings macOS, develop with it: only the old keys go, of all three
#    platforms; nothing in use by either branch, nothing recent, nothing unknown.
rm -rf "$work/branches"
fake_branch main linux=1111111111111111 windows=3333333333333333
fake_branch develop linux=2222222222222222 windows=4444444444444444 macos=5555555555555555
store
prune > "$work/log"
expect_deleted "main without macos, develop with it: only unused keys" \
  sdk-linux-amd64-0000000000000000.tar.zst ogre-linux-amd64-0000000000000000.tar.zst \
  sdk-windows-amd64-0000000000000000.zip ogre-windows-amd64-0000000000000000.zip \
  sdk-macos-arm64-0000000000000000.tar.zst ogre-macos-arm64-0000000000000000.tar.zst

# 2. Both branches with macOS, on different keys: each branch's macOS key is kept.
rm -rf "$work/branches"
fake_branch main linux=1111111111111111 windows=3333333333333333 macos=0000000000000000
fake_branch develop linux=2222222222222222 windows=4444444444444444 macos=5555555555555555
store
prune > "$work/log"
expect_deleted "both branches with macos: both macos keys kept" \
  sdk-linux-amd64-0000000000000000.tar.zst ogre-linux-amd64-0000000000000000.tar.zst \
  sdk-windows-amd64-0000000000000000.zip ogre-windows-amd64-0000000000000000.zip

# 3. No branch with macOS (develop's key.sh lost it, or the store is pruned by an older branch):
#    no macOS archive is deleted, whatever its key; Linux and Windows are pruned as before.
rm -rf "$work/branches"
fake_branch main linux=1111111111111111 windows=3333333333333333
fake_branch develop linux=2222222222222222 windows=4444444444444444
store
prune > "$work/log"
expect_deleted "no branch with macos: no macos archive deleted" \
  sdk-linux-amd64-0000000000000000.tar.zst ogre-linux-amd64-0000000000000000.tar.zst \
  sdk-windows-amd64-0000000000000000.zip ogre-windows-amd64-0000000000000000.zip

# 4. The dry run (--no-delete) deletes nothing.
rm -rf "$work/branches"
fake_branch main linux=1111111111111111 windows=3333333333333333
fake_branch develop linux=2222222222222222 windows=4444444444444444 macos=5555555555555555
store
prune no-delete > "$work/log"
expect_deleted "dry run: nothing deleted"

# 5. The repository's own key.sh gives a key for each of the three platforms.
mkdir -p "$work/repo/tools"
cp -R "$here/../../tools/deps" "$here/../../tools/build-deps" "$work/repo/tools/"
cp -R "$here/../../patches" "$work/repo/"
actual=$(keys_of "$work/repo" | cut -d' ' -f1 | tr '\n' ' ')
if [ "$actual" = "linux windows macos " ]; then
  echo "ok   key.sh: linux, windows and macos keys"
else
  echo "FAIL key.sh: platforms with a key: $actual"
  failures=$((failures + 1))
fi

[ "$failures" = 0 ] || { echo "$failures failed"; exit 1; }
echo "all passed"
