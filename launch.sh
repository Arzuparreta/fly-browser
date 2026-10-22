#!/usr/bin/env bash
# Run native Fly with a development profile, separate from installed Fly.
#
#   ./launch.sh [browser arguments]          local build, else the published release
#   ./launch.sh --pr N [browser arguments]   the test build of pull request N
#   ./launch.sh --release [browser arguments] the published release
#
# A local build comes from an engine checkout (docs/chromium/build.md). Without
# one, or with --pr/--release, the native package is downloaded from GitHub
# Releases, checked against its SHA-256 and kept in
# ${XDG_CACHE_HOME:-~/.cache}/fly-browser-launch. Test builds of pull requests
# are pre-releases tagged test-pr-N (docs/releasing.md).
set -euo pipefail
cd "$(dirname "$0")"
repo=$(pwd -P)
github=Arzuparreta/fly-browser
say() { printf '%s\n' "$*" >&2; }
die() { say "$*"; exit 1; }

channel=
case ${1:-} in
    --pr) [ $# -ge 2 ] || die 'Usage: ./launch.sh --pr <pull request number> [browser arguments]'
          channel=pr-$2; shift 2 ;;
    --pr=*) channel=pr-${1#--pr=}; shift ;;
    --release) channel=release; shift ;;
    -h|--help) sed -n '2,12s/^# \{0,1\}//p' "$0"; exit 0 ;;
esac
case $channel in
    ''|release) ;;
    pr-[0-9]*) [[ ${channel#pr-} =~ ^[0-9]+$ ]] || die 'The pull request number must be a number.' ;;
    *) die 'The pull request number must be a number.' ;;
esac

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    die 'Fly needs a graphical session (DISPLAY or WAYLAND_DISPLAY).'
fi

binary=
find_build() {
    local root=$1 output
    local outputs=(out/FlyNative out/FlyRelease)
    if [ -n "${FLY_ENGINE_OUTPUT:-}" ]; then
        outputs=("$FLY_ENGINE_OUTPUT")
    fi
    for output in "${outputs[@]}"; do
        if [ -x "$root/src/$output/chrome" ]; then
            binary=$root/src/$output/chrome
            return 0
        fi
    done
    return 1
}

# Downloads (or reuses) the package of a GitHub release and sets `binary`.
download_build() {
    local channel=$1 cache tag sums name expected actual dir partial
    [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || die 'Downloaded builds exist for Linux x86_64 only.'
    cache=${XDG_CACHE_HOME:-$HOME/.cache}/fly-browser-launch/$channel
    mkdir -p "$cache"
    cached() {
        # The newest complete download of this channel, for offline starts.
        local found
        found=$(ls -1td "$cache"/*/fly-browser/app/fly 2>/dev/null | head -n1 || true)
        [ -n "$found" ] && binary=$found
    }
    if [ "$channel" = release ]; then
        tag=$(curl -fsSL "https://api.github.com/repos/$github/releases/latest" 2>/dev/null |
              sed -n 's/.*"tag_name": *"\(v[^"]*\)".*/\1/p' | head -n1 || true)
    else
        tag=test-$channel
    fi
    sums=
    if [ -n "$tag" ]; then
        sums=$(curl -fsSL "https://github.com/$github/releases/download/$tag/SHA256SUMS.txt" 2>/dev/null || true)
    fi
    name=$(printf '%s\n' "$sums" | awk '$2 ~ /-native-linux-x64\.tar\.gz$/ {sub(/^\*/, "", $2); print $2; exit}')
    expected=$(printf '%s\n' "$sums" | awk -v name="$name" '$2 == name || $2 == "*" name {print $1; exit}')
    if ! [[ $expected =~ ^[a-f0-9]{64}$ ]]; then
        if cached; then
            say "Could not reach GitHub; starting the last downloaded ${channel/pr-/test build of pull request #} instead."
            return 0
        fi
        if [ "$channel" = release ]; then
            die 'The published release could not be downloaded (no network, or it has no native package).'
        fi
        die "There is no test build of pull request #${channel#pr-}. Publish one on the engine host with: python3 scripts/chromium/test-build.py --pr ${channel#pr-}"
    fi
    dir=$cache/$expected
    if [ -x "$dir/fly-browser/app/fly" ]; then
        binary=$dir/fly-browser/app/fly
        return 0
    fi
    available=$(LC_ALL=C df -Pk "$cache" | awk 'END {print $4}')
    [[ $available =~ ^[0-9]+$ ]] && [ "$available" -ge 1048576 ] ||
        die "At least 1 GiB free is needed in $cache; set XDG_CACHE_HOME to a disk with room."
    partial=$(mktemp -d "$cache/download.XXXXXXXXXX")
    trap 'rm -rf "$partial"' EXIT
    say "Downloading $name ($tag)…"
    curl -fL --progress-bar "https://github.com/$github/releases/download/$tag/$name" -o "$partial/$name" ||
        die "The download of $name failed."
    actual=$(sha256sum "$partial/$name"); actual=${actual%% *}
    [ "$actual" = "$expected" ] || die "$name does not match its SHA-256 checksum; nothing was started."
    mkdir "$partial/unpacked"
    tar -xzf "$partial/$name" -C "$partial/unpacked"
    [ -x "$partial/unpacked/fly-browser/app/fly" ] || die "$name does not contain native Fly."
    # Keep only this download for the channel.
    find "$cache" -mindepth 1 -maxdepth 1 -type d ! -name "$(basename "$partial")" -exec rm -rf {} +
    mv "$partial/unpacked" "$dir"
    rm -rf "$partial"
    trap - EXIT
    binary=$dir/fly-browser/app/fly
}

if [ -z "$channel" ]; then
    if [ -n "${FLY_ENGINE_ROOT:-}" ]; then
        find_build "$FLY_ENGINE_ROOT" || true
    else
        # Support a sibling engine or one alongside the directory of Git checkouts.
        for root in "$repo/../fly-chromium" "$repo/../../fly-chromium" "$HOME/.cache/fly-chromium"; do
            if find_build "$root"; then
                break
            fi
        done
    fi
    if [ -z "$binary" ]; then
        say 'Native Fly is not built on this computer; using the published release. Test a pull request with ./launch.sh --pr <number>.'
        channel=release
    fi
fi
if [ -n "$channel" ]; then
    download_build "$channel"
fi

profile=${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser-dev
# A fresh development profile must not auto-import the owner's installed data.
mkdir -p "$profile/empty-import"
exec "$binary" --fly-native --class=fly-browser-dev \
    --user-data-dir="$profile" --fly-import-from="$profile/empty-import" "$@"
