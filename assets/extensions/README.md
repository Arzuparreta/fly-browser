# Bundled extensions

Fly ships these unpacked upstream releases, copied into each profile on first use of Extensions. Disabled startup does not scan or load them. All bundled folders are discovered by the same installer and exposed in Preferences; there are no hidden bundled extensions.

| Folder | Release | Source | Licence |
| --- | --- | --- | --- |
| `ublock-origin` | 1.75.0, Chromium Manifest V2 | [Official release](https://github.com/gorhill/uBlock/releases/tag/1.75.0), `uBlock0_1.75.0.chromium.zip` | GPL-3.0-or-later, included `LICENSE.txt` |

The unpacked release is unmodified. The archive SHA-256 is recorded below. Source for this release is available at the linked tag; its filter assets and their notices are included in the release archive. Keep upstream licences and notices when updating. No npm runtime dependency is added.

Updates replace only profile copies managed by Fly. Profile paths stay stable, extension storage is retained and disabled state and workspace selections are never reset. Removed bundles have durable tombstones, including folders removed manually. A manually installed folder already using the same name is never overwritten.

Electron needs real unpacked files, so `assets/extensions` is included in `asarUnpack`. Runtime loads the profile copy, never extension code directly from the application bundle.

Archive SHA-256: `393cf95709d1074d4022970e9014e434395c53a822387f1e25f43be97cf4b582`.

The vendored tree is marked in `.gitattributes` to preserve upstream whitespace byte for byte; application code and documentation still use the normal whitespace check. The API preload adapter is generated from the existing `electron-chrome-extensions` dependency by `scripts/generate-extension-preload.js`; it fixes the browser/chrome alias and read-only managed policy storage without changing uBlock files.
