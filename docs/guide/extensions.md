# Chrome extensions

Fly uses Chromium's extension system. Install extensions through the Chrome
Web Store and its permission prompt. Manage extensions in
**Preferences → Extensions** or `chrome://extensions`. Each normal workspace
has its own profile, extension storage and enabled choices. Toolbar actions
appear in the Fly footer and open their popups above it.

## Bundled blocker

Fly includes **uBlock Origin Lite**, pinned by archive checksum and Web Store
identity in `chromium/bundled-extensions.json`. It uses Chromium's Manifest V3
declarative network rules.

New workspaces receive the bundled blocker unless it was removed or disabled.
The bundled component also blocks in private workspaces. Other extensions follow Chromium's
incognito controls; do not assume every installed extension runs privately.

Open the blocker popup from the footer for its controls. Its configuration and
filter capabilities differ from full uBlock Origin. Disabling the component
unloads it; Chromium removes its dynamic rules during uninstall cleanup, while
stored settings remain. Re-enable it and check its settings afterwards.

## Updates

Permission increases may require an explicit prompt in `chrome://extensions`
before re-enabling an extension.

Bundled extension updates arrive with Fly packages. Store extensions use
Chromium's extension machinery. User scripts/styles are implemented by a
separate generated component; see [their guide](/guide/user-scripts-and-styles).
