# User scripts and styles

Native Fly watches `.user.js` and `.user.css` files under its user data directory:

```text
~/.config/fly-browser/scripts/
~/.config/fly-browser/styles/
```

Use the corresponding commands in the Command Centre to open these folders.
With a custom `XDG_CONFIG_HOME`, use that directory instead of `~/.config`.
Fly builds and reloads a **Fly User Content** component extension from the files.
Scripts are registered with Chromium's `chrome.userScripts` API; CSS uses the
extension content machinery. Changes made while Fly was closed also refresh
the generated component on next start.

Standard userscript metadata controls matching, timing and grants. Check
`@match`/`@include`, `@exclude`, `@run-at`, `@grant` and `@connect` when porting a
script. Scripts without GM grants, or with `@inject-into page`, run in the page
world. GM scripts run in isolated user-script worlds; `unsafeWindow` refers to
that isolated world's window, so scripts needing page globals and GM APIs
together may need adaptation.

Supported helpers include GM value storage, network requests, tabs and
notifications. GM values persist in extension storage. `GM_xmlhttpRequest` checks grants, `@connect`, redirect destinations
and local-network access. `GM_registerMenuCommand` records commands but does
not yet expose a menu. Do not assume full Tampermonkey compatibility.

The component also supplies the Share context submenu. Local scripts can read
and change matching pages; install only scripts you trust. Blocking is handled
by [uBlock Origin Lite](/guide/extensions), separately from user content.

For debugging, inspect Chromium's extension and page developer tools. Source
implementation: `chromium/overlay/fly/resources/user_content/` and
`chromium/overlay/fly/ui/user_content.cc`. The archived Electron guide under
`legacy/electron/docs/` explains the previous injection mechanism only.
