# Privacy and blocking

Each workspace has a separate Chromium profile for cookies, cache, storage and
extensions. Private workspaces use off-the-record profiles. Closing private
browsing discards its in-memory site data; downloaded files remain on disk.

Fly bundles **uBlock Origin Lite** for ad and tracker blocking, including private
workspaces. Open its footer popup to adjust it. Turning extensions or the blocker
off removes that protection. See [extensions](/guide/extensions).

Fly keeps product data locally and checks GitHub for release updates. Websites,
installed extensions and Chromium services can make network requests of their
own. Private browsing does not hide your IP or protect against the operating
system. See [privacy policy](/guide/privacy-policy).

## Protected video

Netflix, Spotify, Disney+ and other services that protect their media with
Widevine need Google's Widevine module. Fly does not ship it: about a minute
after Fly starts for the first time, Chromium's component updater downloads it
from Google into the `WidevineCdm` folder of your Fly data directory and keeps
it updated. Restart Fly once after that download; protected video plays from
the next start on, in every workspace.
