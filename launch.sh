#!/usr/bin/env bash
# Run the local native build with a development profile, separate from installed Fly.
set -euo pipefail
cd "$(dirname "$0")"
repo=$(pwd -P)
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
    echo "Native Fly is not built in the local engine checkout. See docs/chromium/build.md; set FLY_ENGINE_ROOT and FLY_ENGINE_OUTPUT for a different location." >&2
    exit 1
fi
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "Fly needs a graphical session (DISPLAY or WAYLAND_DISPLAY)." >&2
    exit 1
fi
profile=${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser-dev
# A fresh development profile must not auto-import the owner's installed data.
mkdir -p "$profile/empty-import"
exec "$binary" --fly-native --class=fly-browser-dev \
    --user-data-dir="$profile" --fly-import-from="$profile/empty-import" "$@"
