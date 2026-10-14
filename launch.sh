#!/usr/bin/env bash
# Start Fly from a source checkout. Dependencies are installed on the first run and
# again whenever a pull changes them, so a fresh clone starts without extra steps.
cd "$(dirname "$0")" || exit 1

electron=./node_modules/.bin/electron
installed=node_modules/.package-lock.json
if [ ! -x "$electron" ] || [ ! -f "$installed" ] || [ package-lock.json -nt "$installed" ] || [ package.json -nt "$installed" ]; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "Fly needs Node.js and npm to run from source: https://nodejs.org/" >&2
        exit 1
    fi
    echo "Installing Fly's dependencies (first run or after an update)..."
    npm ci --no-audit --no-fund || { echo "Could not install Fly's dependencies." >&2; exit 1; }
fi

# Electron 44 no longer downloads its binary during installation; fetch it once here.
./node_modules/.bin/install-electron || { echo "Could not download Electron." >&2; exit 1; }

# Use the project's pinned Electron rather than letting npx fetch an arbitrary version.
exec "$electron" . --log-level=3 "$@"
