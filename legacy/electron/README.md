# Electron legacy

The Electron implementation remains in the repository root for comparison,
regression tests and recovery. It is no longer the default runtime or release.
Keeping its paths avoids breaking shared assets, generated keyboard contracts
and the native importer. Do not use an Electron profile as a Chromium data dir.

- Run: `npm ci && npm run start:legacy` (or `bash legacy/electron/launch.sh`).
- Package locally: `npm run dist:legacy -- --linux --publish never`.
- Old installer: `bash legacy/electron/fly-install status`.
- Archived release recipe: `legacy/electron/release.yml` (inactive).
- Archived implementation guides: `legacy/electron/docs/`.

Legacy source includes `main.js`, `renderer.js`, `preload.js`, `modules/`,
Electron services and UI tests. Shared assets and catalogues still feed native
Fly; retain their generation checks. Electron uses `~/.config/fly-browser`;
native Fly uses `$XDG_CONFIG_HOME/fly-browser-native` (default `~/.config`).
Legacy binaries are not published by the active release workflow. Older GitHub
releases remain available. No live user profile is removed by the cutover.
