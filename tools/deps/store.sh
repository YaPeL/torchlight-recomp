#!/usr/bin/env bash
# The prebuilt SDK and OGRE as assets of one prerelease, "deps", never marked latest, so the
# repository's release page shows the game's releases (docs/release-pipeline.md, 5.9). Each asset
# carries its platform and key (tools/deps/key.sh): sdk-linux-amd64-<key>.tar.zst,
# ogre-linux-amd64-<key>.tar.zst, sdk-windows-amd64-<key>.zip, ogre-windows-amd64-<key>.zip.
#
#   store.sh has PLATFORM KEY             exit 0 when both assets of KEY are published
#   store.sh fetch PLATFORM KEY DIR       download them into DIR (exit 1 when missing)
#   store.sh publish PLATFORM KEY SDK OGRE  upload the two archives as KEY's assets
#   store.sh prune [--no-delete]          migrate the old deps-<key> releases, then remove what
#                                         neither main nor develop uses (see prune below);
#                                         --no-delete migrates and only reports the deletions
#
# PLATFORM is linux or windows. Needs gh (GH_TOKEN) and GITHUB_REPOSITORY; prune also git. Only
# `gh api`, `gh release upload` and `gh release download -p` are used: Ubuntu 22.04's gh (2.4) has
# no --json on release list and no --latest.
set -euo pipefail

STORE=deps
REPO=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}
# An asset uploaded less than this long ago is never pruned: a concurrent run (develop and main
# after a freeze) may be publishing a key the branches it read did not have yet.
GRACE_SECONDS=$((24 * 3600))

names() {  # PLATFORM KEY -> the two asset names
  case $1 in
    linux) echo "sdk-linux-amd64-$2.tar.zst ogre-linux-amd64-$2.tar.zst" ;;
    windows) echo "sdk-windows-amd64-$2.zip ogre-windows-amd64-$2.zip" ;;
    *) echo "unknown platform $1" >&2; exit 2 ;;
  esac
}

assets() {  # the store's assets: id, name, updated_at (tab separated); nothing when it is missing
  gh api "repos/$REPO/releases/tags/$STORE" --jq '.assets[] | [.id, .name, .updated_at] | @tsv' \
    2> /dev/null || true
}

store_exists() { gh api "repos/$REPO/releases/tags/$STORE" > /dev/null 2>&1; }
ensure_store() {  # creates the store; a concurrent run may have created it first
  store_exists && return 0
  gh api -X POST "repos/$REPO/releases" -f tag_name="$STORE" -f name="Dependencies" \
    -F prerelease=true -f make_latest=false \
    -f body="The patched ReXGlue SDK and OGRE 14.6.0 that CI builds with tools/deps/ and tools/build-deps/, one pair of archives per platform and key (tools/deps/key.sh). Nothing of the game. Not a release of the game: CI keeps only what main and develop use." \
    > /dev/null 2>&1 || store_exists
}
old_releases() {  # the tags of the old per-key releases (deps-<key>, deps-windows-<key>)
  local tags
  tags=$(gh api "repos/$REPO/releases?per_page=100" --jq '.[].tag_name')
  grep -E '^deps-' <<< "$tags" || true
}
delete_release() {  # TAG: the release and its tag
  local id
  id=$(gh api "repos/$REPO/releases/tags/$1" --jq .id)
  gh api -X DELETE "repos/$REPO/releases/$id" > /dev/null
  gh api -X DELETE "repos/$REPO/git/refs/tags/$1" > /dev/null 2>&1 || true
}

has() {
  local list name
  list=$(assets | cut -f2)
  for name in $(names "$1" "$2"); do
    grep -qxF "$name" <<< "$list" || return 1
  done
}

fetch() {
  local name
  has "$1" "$2" || return 1
  mkdir -p "$3"
  for name in $(names "$1" "$2"); do
    rm -f "$3/$name"
    gh release download "$STORE" -R "$REPO" -p "$name" -D "$3"
  done
}

publish() {  # PLATFORM KEY SDK_ARCHIVE OGRE_ARCHIVE
  local dir sdk ogre
  read -r sdk ogre <<< "$(names "$1" "$2")"
  dir=$(mktemp -d)
  cp "$3" "$dir/$sdk"
  cp "$4" "$dir/$ogre"
  ensure_store
  gh release upload "$STORE" -R "$REPO" --clobber "$dir/$sdk" "$dir/$ogre"
  rm -rf "$dir"
}

# The keys main and develop use now, read from each branch's own key.sh ("linux KEY" and
# "windows KEY" lines), and whether a branch's workflow still reads the old deps-<key> releases.
branch_keys() {
  local ref dir
  git fetch -q --depth=1 origin main:refs/remotes/origin/main develop:refs/remotes/origin/develop
  for ref in origin/main origin/develop; do
    dir=$(mktemp -d)
    git archive "$ref" tools/deps tools/build-deps patches | tar -x -C "$dir"
    echo "linux $(sh "$dir/tools/deps/key.sh" linux)"
    echo "windows $(sh "$dir/tools/deps/key.sh" windows)"
    rm -rf "$dir"
  done | sort -u
}
old_scheme_keys() {  # the keys of the branches whose CI still downloads deps-<key> releases
  local ref dir
  for ref in origin/main origin/develop; do
    git show "$ref:.github/workflows/ci.yml" | grep -q 'release download "deps-\$key"' || continue
    dir=$(mktemp -d)
    git archive "$ref" tools/deps tools/build-deps patches | tar -x -C "$dir"
    echo "linux $(sh "$dir/tools/deps/key.sh" linux)"
    echo "windows $(sh "$dir/tools/deps/key.sh" windows)"
    rm -rf "$dir"
  done | sort -u
}

# 1. Migration: each old release deps-<key> (Linux) or deps-windows-<key> whose key main or develop
#    uses gets its archives copied into the store under the new names; old releases that are gone
#    are simply not there. 2. The old releases are deleted with their tags, except those a branch
#    still reads (main keeps the old scheme until the freeze that brings this), and so are the
#    deps-* tags left without a release. 3. The store's assets whose key neither branch uses are
#    deleted, unless uploaded within the grace period.
#    The keys are read again right before each deletion step, so a run never deletes what the
#    branches use at that moment.
prune() {
  local dry=${1:-} keys keep old tag platform key dir name id updated now age list
  keys=$(branch_keys)
  echo "keys in use:"; sed 's/^/  /' <<< "$keys"
  old=$(old_releases)
  for tag in $old; do
    case $tag in
      deps-windows-*) platform=windows; key=${tag#deps-windows-} ;;
      *) platform=linux; key=${tag#deps-} ;;
    esac
    if grep -qxF "$platform $key" <<< "$keys" && ! has "$platform" "$key"; then
      echo "migrate $tag"
      dir=$(mktemp -d)
      gh release download "$tag" -R "$REPO" -D "$dir"
      publish "$platform" "$key" "$(ls "$dir"/sdk-*)" "$(ls "$dir"/ogre-*)"
      rm -rf "$dir"
    fi
  done
  keep=$(old_scheme_keys)
  old=$(old_releases)
  for tag in $old; do
    case $tag in
      deps-windows-*) platform=windows; key=${tag#deps-windows-} ;;
      *) platform=linux; key=${tag#deps-} ;;
    esac
    if grep -qxF "$platform $key" <<< "$keep"; then
      echo "keep $tag (a branch still reads the old scheme)"
      continue
    fi
    # Deleted only once its archives are in the store, or when no branch uses its key.
    if grep -qxF "$platform $key" <<< "$keys" && ! has "$platform" "$key"; then
      echo "keep $tag (not migrated yet)"
      continue
    fi
    echo "delete release $tag"
    [ -n "$dry" ] || delete_release "$tag"
  done
  # Tags left behind by old releases deleted elsewhere (from the web page a release goes, its tag
  # stays): deleted unless a branch still reads that release.
  local tags
  old=$(old_releases)
  tags=$(gh api "repos/$REPO/git/matching-refs/tags/deps-" --jq '.[].ref')
  for tag in $(sed 's|^refs/tags/||' <<< "$tags"); do
    grep -qxF "$tag" <<< "$old" && continue
    case $tag in
      deps-windows-*) platform=windows; key=${tag#deps-windows-} ;;
      *) platform=linux; key=${tag#deps-} ;;
    esac
    grep -qxF "$platform $key" <<< "$keep" && continue
    echo "delete tag $tag (no release)"
    [ -n "$dry" ] || gh api -X DELETE "repos/$REPO/git/refs/tags/$tag" > /dev/null
  done
  keys=$(branch_keys)
  list=$(assets)
  now=$(date -u +%s)
  while IFS=$'\t' read -r id name updated; do
    [ -n "$id" ] || continue
    key=$(sed -E 's/^(sdk|ogre)-(linux|windows)-amd64-([0-9a-f]+)\..*/\2 \3/' <<< "$name")
    grep -qxF "$key" <<< "$keys" && continue
    age=$((now - $(date -u -d "$updated" +%s)))
    if [ "$age" -lt "$GRACE_SECONDS" ]; then
      echo "keep $name (uploaded ${age}s ago)"
      continue
    fi
    echo "delete asset $name"
    [ -n "$dry" ] || gh api -X DELETE "repos/$REPO/releases/assets/$id" > /dev/null
  done <<< "$list"
}

case ${1:-} in
  has) has "$2" "$3" ;;
  fetch) fetch "$2" "$3" "$4" ;;
  publish) publish "$2" "$3" "$4" "$5" ;;
  prune) prune "${2:+no-delete}" ;;
  *) sed -n '2,15p' "$0" >&2; exit 2 ;;
esac
