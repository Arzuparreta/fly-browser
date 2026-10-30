# Privacy policy

Fly does not collect product telemetry or usage analytics. Product settings,
workspace state and browsing data are kept locally in
`$XDG_CONFIG_HOME/fly-browser` (default `~/.config/fly-browser`).

## Network activity

Fly checks GitHub for release updates and downloads an update when requested.
The installer also contacts GitHub for packages and checksums. These requests
expose normal connection information such as your IP to the service provider.
Websites, search engines, installed extensions, the bundled blocker and Chromium
services make requests needed for their features. Chromium's component updater
downloads data and modules from Google, including the Widevine module that
protected video services such as Netflix and Spotify need.
Google Safe Browsing downloads lists of dangerous sites from Google. When an
address matches one, it sends Google a partial hash of it; for suspicious pages
and some downloads, Chromium sends Google the address and details of the page
or file. Google sees your IP for these requests. Turn it off in
`chrome://settings/security`.
Translating a page sends its text to Google Translate; Fly only offers it, and
detects the page language locally. Spell checking downloads the dictionary of
each language you choose from Google the first time it is needed and checks
your text locally; Chromium's **Use enhanced spell check**, off unless you turn
it on, sends what you type to Google. Zero Fly telemetry is not a
promise of zero network activity.

## Isolation and blocking

Each workspace has a separate Chromium profile for cookies, site storage and
extensions. Private browsing uses off-the-record profiles and discards their
site state when closed. It does not hide your IP, erase downloads or protect
against software on your machine. uBlock Origin Lite is bundled, including in
private workspaces, unless disabled or removed. Other extensions follow
Chromium's incognito permissions.

Fly runs Chromium's default features, not the experiments Chromium enables in
its development builds. Chromium's advertising APIs (Topics, Protected
Audience, Attribution Reporting and Shared Storage) are turned off, and the
HTTP cache and network connections stay partitioned by site.

## Local data

User scripts and styles can read or modify matching pages; review them before
installing.

Uninstalling retains profiles. To back up or remove browsing data, close Fly
first and handle the complete profile directory. Cookie encryption and other
storage protections depend on the operating system and Chromium configuration;
do not assume that a profile backup is encrypted. No Fly cloud sync service is
provided.

Questions and vulnerabilities: use the project's GitHub issues for general
questions and private Security Advisories for sensitive reports.
