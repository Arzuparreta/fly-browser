#!/usr/bin/env bash
# Start the native Chromium port. Electron is available via npm run start:legacy.
set -euo pipefail
cd "$(dirname "$0")"
root=${FLY_ENGINE_ROOT:-$HOME/.cache/fly-chromium}
output=${FLY_ENGINE_OUTPUT:-out/FlyRelease}
binary=$root/src/$output/chrome
if [ ! -x "$binary" ]; then
    echo "Native Fly is not built at $binary. See docs/chromium/build.md; set FLY_ENGINE_ROOT and FLY_ENGINE_OUTPUT for your checkout." >&2
    exit 1
fi
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "Fly needs a graphical session (DISPLAY or WAYLAND_DISPLAY)." >&2
    exit 1
fi
exec "$binary" --fly-native --class=fly-browser \
    --user-data-dir="${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser" "$@"
