#!/usr/bin/env bash
# Start Fly from a source checkout. Dependencies are installed on the first run and
# again whenever a pull changes them, so a fresh clone starts without extra steps.
cd "$(dirname "$0")/../.." || exit 1

electron=./node_modules/.bin/electron
installed=node_modules/.package-lock.json
if [ ! -x "$electron" ] || [ ! -f "$installed" ] || [ package-lock.json -nt "$installed" ] || [ package.json -nt "$installed" ]; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "Fly needs Node.js and npm to run from source: https://nodejs.org/" >&2
        exit 1
    fi
    # Reinstalling deletes node_modules, which would pull the files out from under a Fly
    # already running from this checkout and leave it broken while it holds the
    # single-instance lock. ENVIRON keeps the path out of awk's own arguments.
    if ps -eo args= 2>/dev/null |
        FLY_ELECTRON_DIR="$(pwd -P)/node_modules/electron/dist/" awk 'index($0, ENVIRON["FLY_ELECTRON_DIR"]) == 1 { found = 1 } END { exit !found }'; then
        echo "Fly is already running from this folder. Close it so its dependencies can be updated, then run launch.sh again." >&2
        exit 1
    fi
    echo "Installing Fly's dependencies (first run or after an update)..."
    npm ci --no-audit --no-fund || { echo "Could not install Fly's dependencies." >&2; exit 1; }
fi

# Electron 44 no longer downloads its binary during installation; fetch it once here.
./node_modules/.bin/install-electron || { echo "Could not download Electron." >&2; exit 1; }

# Without a display Electron exits silently, since --log-level=3 hides its error. This
# happens over SSH, or in a tmux pane opened from SSH, where DISPLAY is not set.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "Fly needs a graphical session, but neither DISPLAY nor WAYLAND_DISPLAY is set in this terminal. To open Fly on this machine's screen from SSH or tmux, run: DISPLAY=:0 ./launch.sh" >&2
    exit 1
fi

# Use the project's pinned Electron rather than letting npx fetch an arbitrary version.
exec "$electron" . --log-level=3 "$@"
