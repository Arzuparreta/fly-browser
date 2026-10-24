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
script=$(readlink -f "$0")
cd "$(dirname "$script")"
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
    -h|--help) sed -n '2,12s/^# \{0,1\}//p' "$script"; exit 0 ;;
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

# True when a running process executes a file below directory $1.
in_use() {
    local exe
    for exe in /proc/[0-9]*/exe; do
        exe=$(readlink "$exe" 2>/dev/null) || continue
        case $exe in "$1"/*) return 0 ;; esac
    done
    return 1
}
remove_unused() {
    if ! in_use "$1"; then rm -rf "$1"; fi
}
# Removes the other downloads in the channel of build directory $1, except
# while a Fly runs from them.
prune_channel() {
    local old
    for old in "$(dirname "$1")"/*; do
        if [ "$old" != "$1" ] && [[ $(basename "$old") =~ ^[a-f0-9]{64}$ ]]; then
            remove_unused "$old"
        fi
    done
}

# Downloads (or reuses) the package of a GitHub release and sets `binary`.
download_build() {
    local channel=$1 root cache what tag sums name expected actual dir partial old
    [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || die 'Downloaded builds exist for Linux x86_64 only.'
    root=${XDG_CACHE_HOME:-$HOME/.cache}/fly-browser-launch
    mkdir -p "$root/$channel"
    # Running browsers report resolved paths; compare against resolved paths.
    root=$(cd "$root" && pwd -P)
    cache=$root/$channel
    if [ "$channel" = release ]; then
        what='the published release'
    else
        what="the test build of pull request #${channel#pr-}"
    fi
    # Test builds of pull requests not started for 30 days go.
    for old in "$root"/pr-*; do
        if [ -d "$old" ] && [ "$old" != "$cache" ] && [ -n "$(find "$old" -maxdepth 0 -mtime +30)" ]; then
            remove_unused "$old"
        fi
    done
    cached() {
        # The newest complete download of this channel.
        local found
        found=$(ls -1td "$cache"/*/fly-browser/app/fly 2>/dev/null | head -n1 || true)
        [ -n "$found" ] && binary=$found
    }
    # GitHub's answer: 200 found, 404 not published, anything else (000 when
    # unreachable, rate limits, server errors) means "try the cached copy".
    fetch() {
        http=$(curl -sSL -o "$2" -w '%{http_code}' "$1" 2>/dev/null) || http=000
    }
    local http=200 answer
    answer=$(mktemp "$cache/download.answer.XXXXXXXXXX")
    tag= sums=
    if [ "$channel" = release ]; then
        fetch "https://api.github.com/repos/$github/releases/latest" "$answer"
        if [ "$http" = 200 ]; then
            tag=$(sed -n 's/.*"tag_name": *"\(v[^"]*\)".*/\1/p' "$answer" | head -n1)
            [ -n "$tag" ] || http=404
        fi
    else
        tag=test-$channel
    fi
    if [ "$http" = 200 ]; then
        fetch "https://github.com/$github/releases/download/$tag/SHA256SUMS.txt" "$answer"
        [ "$http" != 200 ] || sums=$(cat "$answer")
    fi
    rm -f "$answer"
    name=$(printf '%s\n' "$sums" | awk '$2 ~ /-native-linux-x64\.tar\.gz$/ {sub(/^\*/, "", $2); print $2; exit}')
    expected=$(printf '%s\n' "$sums" | awk -v name="$name" '$2 == name || $2 == "*" name {print $1; exit}')
    if ! [[ $expected =~ ^[a-f0-9]{64}$ ]]; then
        if [ "$http" = 200 ] || [ "$http" = 404 ]; then
            if [ "$channel" = release ]; then
                die 'The published release has no native package to download.'
            fi
            # A pull request's test build is deleted when it closes.
            remove_unused "$cache"
            die "There is no test build of pull request #${channel#pr-}. The maintainer publishes test builds of open pull requests (docs/releasing.md)."
        fi
        if cached; then
            say "Could not download from GitHub (HTTP $http); starting the last downloaded copy of $what."
            touch "$cache"
            return 0
        fi
        if [ "$http" = 000 ]; then
            die "Could not reach GitHub to download $what. Check the network connection."
        fi
        die "GitHub did not provide $what (HTTP $http). Try again later."
    fi
    dir=$cache/$expected
    if [ -x "$dir/fly-browser/app/fly" ]; then
        binary=$dir/fly-browser/app/fly
        touch "$cache"
        prune_channel "$dir"
        return 0
    fi
    available=$(LC_ALL=C df -Pk "$cache" | awk 'END {print $4}')
    [[ $available =~ ^[0-9]+$ ]] && [ "$available" -ge 1048576 ] ||
        die "At least 1 GiB free is needed in $cache; set XDG_CACHE_HOME to a disk with room."
    find "$cache" -mindepth 1 -maxdepth 1 -name 'download.*' -mmin +60 -exec rm -rf {} +
    partial=$(mktemp -d "$cache/download.XXXXXXXXXX")
    trap 'rm -rf "$partial"' EXIT
    say "Downloading $name ($tag)…"
    if ! curl -fL --progress-bar "https://github.com/$github/releases/download/$tag/$name" -o "$partial/$name"; then
        rm -rf "$partial"
        trap - EXIT
        if cached; then
            say "The download failed; starting the last downloaded copy of $what."
            return 0
        fi
        die "The download of $name failed."
    fi
    actual=$(sha256sum "$partial/$name"); actual=${actual%% *}
    [ "$actual" = "$expected" ] || die "$name does not match its SHA-256 checksum; nothing was started."
    mkdir "$partial/unpacked"
    tar -xzf "$partial/$name" -C "$partial/unpacked"
    [ -x "$partial/unpacked/fly-browser/app/fly" ] || die "$name does not contain native Fly."
    # Another start may have finished the same download meanwhile; then this
    # copy is dropped.
    mv -T "$partial/unpacked" "$dir" 2>/dev/null || true
    rm -rf "$partial"
    trap - EXIT
    [ -x "$dir/fly-browser/app/fly" ] || die "$name could not be unpacked into $dir."
    prune_channel "$dir"
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
# Chromium hands a second start over to the browser already using the
# profile: with another build running, this build would not start at all.
if lock=$(readlink "$profile/SingletonLock" 2>/dev/null) && [ "${lock%-*}" = "$(uname -n)" ]; then
    pid=${lock##*-}
    # Only a browser started with this profile counts; a stale lock whose
    # process number now belongs to another program does not. Chromium
    # rewrites its command line as one space-separated string.
    command=
    if [[ $pid =~ ^[0-9]+$ ]] && running=$(readlink "/proc/$pid/exe" 2>/dev/null); then
        command=$( (tr '\0' ' ' < "/proc/$pid/cmdline") 2>/dev/null || true)
    fi
    if [[ " $command " == *" --user-data-dir=$profile "* ]] &&
        [ "$running" != "$(readlink -f "$binary")" ]; then
        die "A development Fly from another build is running (${running}). Close it, then start this one."
    fi
fi
exec "$binary" --fly-native --class=fly-browser-dev \
    --user-data-dir="$profile" "$@"
