# Privacy and blocking

Each workspace has a separate Chromium profile for cookies, cache, storage and
extensions. Private workspaces use off-the-record profiles. Closing private
browsing discards its in-memory site data; downloaded files remain on disk.

Fly bundles **uBlock Origin Lite** for ad and tracker blocking, including private
workspaces. Open its footer popup to adjust it. Turning extensions or the blocker
off removes that protection. Full uBlock Origin settings from Electron cannot
be imported as Lite settings. See [extensions](/guide/extensions).

Fly keeps product data locally and checks GitHub for release updates. Websites,
installed extensions and Chromium services can make network requests of their
own. Private browsing does not hide your IP or protect against the operating
system. See [privacy policy](/guide/privacy-policy).
