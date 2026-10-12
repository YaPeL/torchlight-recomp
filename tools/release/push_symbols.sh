#!/usr/bin/env bash
# Pushes one platform's debug symbols of a release to the private symbols repository
# (docs/release-pipeline.md, decision 4), as one compressed archive: GitHub refuses files over
# 100 MB, and the macOS executable's DWARF alone is larger. Used by release.yml's three game jobs.
#
# Usage: push_symbols.sh SYMBOLS_DIR TAG PLATFORM REPOSITORY
#   SYMBOLS_DIR  the folder of symbols to archive (its contents, as they are)
#   TAG          the release's tag (v0.7.6-beta): the archive goes to TAG/PLATFORM/
#   PLATFORM     linux-x86_64, windows-x86_64 or macos-arm64
#   REPOSITORY   the repository to clone and push to (a git URL or a local path)
# GIT_SSH_COMMAND, when set, is how git reaches REPOSITORY.
#
# The archive is symbols.tar.zst (zstd), or symbols.tar.xz where zstd is missing. Over
# SYMBOLS_LIMIT_MB (95, below GitHub's 100 MB) it stops before cloning, with the size. Idempotent:
# a re-run of the same tag (a retried job, a release candidate run again) replaces what TAG/PLATFORM
# holds, and a repository that already has the same files is left as it is. On a push race with
# the other platforms' jobs it rebases onto theirs (different folders, so nothing conflicts).
set -euo pipefail

[ $# -eq 4 ] || { echo "usage: $0 SYMBOLS_DIR TAG PLATFORM REPOSITORY" >&2; exit 2; }
symbols=$1 tag=$2 platform=$3 repository=$4
limit_mb=${SYMBOLS_LIMIT_MB:-95}
[ -d "$symbols" ] || { echo "push_symbols: no folder $symbols" >&2; exit 1; }
[ -n "$(ls -A "$symbols")" ] || { echo "push_symbols: $symbols is empty" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if command -v zstd > /dev/null; then
  archive="$work/symbols.tar.zst"
  tar -cf - -C "$symbols" . | zstd -q -19 -T0 -o "$archive"
else
  archive="$work/symbols.tar.xz"
  tar -cf - -C "$symbols" . | xz -9 -T0 > "$archive"
fi
size=$(wc -c < "$archive" | tr -d ' ')
echo "push_symbols: $platform: $(basename "$archive") is $((size / 1048576)) MB"
if [ "$size" -gt $((limit_mb * 1048576)) ]; then
  echo "push_symbols: $platform's compressed symbols are $((size / 1048576)) MB, over the" \
       "${limit_mb} MB limit (GitHub refuses files over 100 MB): split them per file" >&2
  exit 1
fi

git clone -q --depth 1 "$repository" "$work/repo"
cd "$work/repo"
git config user.name "release workflow"
git config user.email "release@localhost"
git rm -rq --ignore-unmatch "$tag/$platform"
rm -rf "${tag:?}/${platform:?}"
mkdir -p "$tag/$platform"
cp "$archive" "$tag/$platform/"
git add -A "$tag/$platform"
if git diff --cached --quiet; then
  echo "push_symbols: $tag/$platform already holds these symbols"
  exit 0
fi
git commit -q -m "Symbols of $tag, $platform"
for attempt in 1 2 3; do
  git push -q origin HEAD && exit 0
  [ "$attempt" = 3 ] && break
  git pull -q --rebase origin HEAD
done
echo "push_symbols: the push of $tag/$platform failed three times" >&2
exit 1
