# Workspaces and containers

Each native Fly workspace owns a separate Chromium profile. Cookies, logins,
site storage and extensions are isolated between profiles, while Fly presents
them in one native window. Renaming a workspace keeps its profile and logins.
Private workspaces use off-the-record profiles.

Use the Workspace Dashboard to create, rename or delete workspaces and move
tabs. Switching workspaces changes the visible native tab hosts. Split view
shows two native pages; commands target the focused pane. Moving a tab between
profiles can require a reload and uses the destination's login state.

The first imported workspace uses `Default`, and later profiles are managed by
Chromium under `$XDG_CONFIG_HOME/fly-browser`. Do not rename profile
folders or edit Chromium files while Fly is running. Fly's workspace registry
is stored in `Local State`; it is no longer Electron's `session.json` with
`persist:*` partitions. See [configuration](/reference/configuration).

The first native start imports supported legacy workspace data without changing
the source. See [migration](/guide/installation#moving-from-electron).
