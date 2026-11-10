# Workspaces and containers

Each Fly workspace owns a separate Chromium profile and opens in its own
window. Cookies, logins, site storage and extensions are isolated between
profiles. Renaming a workspace keeps its profile and logins. Private
workspaces use off-the-record profiles and also open in their own window.

Every Fly window has the full Fly interface: sidebar, tab list, status bar and
panels. Workspaces are numbered in the order of their list. **Ctrl + 1** to
**Ctrl + 9** go to the workspace with that number, and **Alt + PageDown** and
**Alt + PageUp** to the next and previous one. The number of a window's
workspace is on a cell at the top of the collapsed sidebar, and its name is in
the status bar. Use the workspace list in the sidebar or the panel of
workspaces and tabs (**w**, or **Ctrl + Shift + W**) to create, rename or
delete workspaces and move tabs. Choosing a workspace brings its window to the
front, or opens a window if it has none. Closing a window's
last tab closes that window; Fly keeps running while another window is open.
Split view shows two native pages; commands target the focused pane. Moving a
tab between profiles can require a reload and uses the destination's login
state.

The first workspace uses the `Default` profile, and later profiles are managed
by Chromium under `$XDG_CONFIG_HOME/fly-browser`. Do not rename profile folders
or edit Chromium files while Fly is running. Fly's workspace registry is stored
in `Local State`. See [configuration](/reference/configuration).
