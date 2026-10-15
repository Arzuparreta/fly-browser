# Privacy & ad blocking

Fly includes uBlock Origin for ad and tracker blocking. Its popup, filter lists and site exceptions provide one place to control filtering. The former Ghostery blocker and per-tab shields have been removed: running separate request blockers caused conflicts, and a blocked CNN video configuration request triggered an unbounded retry loop.

## uBlock Origin

New profiles enable uBlock by default. Upgrades preserve saved extension settings: if extensions were disabled, enable them in Preferences to use uBlock. Individual disables, removals and workspace selections are preserved.

Open **Preferences → Extensions** to enable or disable uBlock Origin and choose its workspaces. Open its popup from the status bar to pause blocking on a site, or its options to manage lists and custom rules. Fly's trusted-site list does not change uBlock's filtering.

New tabs wait for uBlock's filters to become ready before navigating. Existing tabs need a reload after enabling an extension to receive its content scripts.

Extensions run only in persistent workspace sessions. **Private browsing has no ad blocker**: Electron cannot load extensions in the in-memory private session. Disabling extensions, disabling or removing uBlock, or deselecting it in a workspace also leaves that session without ad blocking.

## Private browsing (`Ctrl + Shift + P`)

Navigation uses the in-memory `FlyPrivateProfile` partition. Its cookies, cache and site storage are discarded when that session ends. Workspaces use separate persistent sessions for ordinary browsing.

## Browser identity and page APIs

Fly removes its application and Electron tokens from its default User-Agent. Google's sign-in page receives Electron's original identity for compatibility. Fly sends Do Not Track and Global Privacy Control preferences to pages; sites decide whether to honour them.

Fly leaves Canvas, WebGL, audio, hardware concurrency, language and time zone APIs intact. The former shield layer modified page data and could interfere with rendering, media and site compatibility. Fly does not claim fingerprint anonymity.

## Site permission exceptions

In **Preferences → Privacy & security**, the trusted-site list controls permission exceptions for capabilities such as camera, microphone and location. Entries include subdomains. Existing entries remain saved when upgrading. This list is independent of uBlock's allowlist.

## Clear site data

In **Preferences → Privacy & security**, use **Clear site cookies** for the active domain, or **Clear cache** for the current session.
