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

## Dangerous sites and downloads

Fly uses Google Safe Browsing. It keeps Google's lists of phishing and malware
sites on your computer, in hashed form, and shows a red **Dangerous site** page
instead of a page on those lists. Files from addresses on the malware lists are
blocked. When an address matches the start of a listed hash, Fly sends that
partial hash, not the address, to Google to confirm it. For some file types
Chromium also asks Google's download reputation service, sending the file's
address and hash; that service only answers Chrome, so those files are not
checked beyond the lists.

To turn it off, open `chrome://settings/security` and choose
**No protection**. **Enhanced protection** sends visited addresses to Google
and is off by default.

## Translation

On a page in a language you do not read, Fly offers Google Translate in a bubble
at the top right. The page language is detected on your computer; the page's
text goes to Google only when you translate it. Choose **Never translate** in
the bubble's menu for a language or site, or turn the offer off in
`chrome://settings/languages`.

## Protected video

Netflix, Spotify, Disney+ and other services that protect their media with
Widevine need Google's Widevine module. Fly does not ship it: about a minute
after Fly starts for the first time, Chromium's component updater downloads it
from Google into the `WidevineCdm` folder of your Fly data directory and keeps
it updated. Restart Fly once after that download; protected video plays from
the next start on, in every workspace.
