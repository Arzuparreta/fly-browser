# Workspaces and containers

Each Fly workspace owns a separate Chromium profile. Cookies, logins,
site storage and extensions are isolated between profiles, while Fly presents
them in one window. Renaming a workspace keeps its profile and logins.
Private workspaces use off-the-record profiles.

Use the Workspace Dashboard to create, rename or delete workspaces and move
tabs. Switching workspaces changes the visible native tab hosts. Split view
shows two native pages; commands target the focused pane. Moving a tab between
profiles can require a reload and uses the destination's login state.

The first workspace uses the `Default` profile, and later profiles are managed
by Chromium under `$XDG_CONFIG_HOME/fly-browser`. Do not rename profile folders
or edit Chromium files while Fly is running. Fly's workspace registry is stored
in `Local State`. See [configuration](/reference/configuration).
