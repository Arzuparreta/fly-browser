# Privacy Policy

**Effective Date:** 30 September 2026  
**Applies To:** Fly Browser (Electron Edition)

Fly Browser is designed with a fundamental promise: **your browsing activity belongs exclusively to you, and your data stays on your local machine.**

---

## Zero Telemetry & Zero Profiling

Fly Browser **does not collect, transmit, sell, or monitor any personal data or usage metrics**:

* **No Analytics:** There are no tracking SDKs, telemetry beacons, or metric collectors built into Fly.
* **No Remote Crash Reports:** The browser does not transmit crash traces, memory dumps, or error logs to any remote server.
* **No Profiling or Phone-Home:** The application binary never phones home to track user identity, IP history, or usage statistics. To keep you informed of critical security patches, Fly performs an anonymous HTTPS query directly to the public GitHub Releases API (`api.github.com`) once every 24 hours. This query transmits zero personal identifiers, browsing data, search tokens, or system fingerprints.
* **No Search Interception:** Search queries entered into the floating address bar or Command Palette route directly and exclusively to your chosen search engine (defaulting to DuckDuckGo).

---

## Local Data Storage

All data created during your browsing sessions is stored strictly on your local filesystem under `~/.config/fly-browser/`:

| File | Purpose | Storage Format |
| :--- | :--- | :--- |
| `config.json` | Hardware settings, sleep timeout, trusted domains, split view ratio | Plaintext JSON |
| `session.json` | Workspaces, active tab URLs, titles, hibernate scroll offsets, split view state | Plaintext JSON |
| `history.json` | Browsing history with page titles and visit timestamps | Plaintext JSON |
| `bookmarks.json` | Saved bookmarks and custom folder trees | Plaintext JSON |
| `quickmarks.json` | Single-key speed-dial navigation mappings | Plaintext JSON |
| `keybinds.json` | Customised keyboard shortcut mappings | Plaintext JSON |
| `user.css` | Optional user stylesheet overrides | Plaintext CSS |
| `user.js` | Optional user script automation scripts | Plaintext JavaScript |

Persistent Chromium session data, cookies, and local storage caches reside in workspace partition folders (`Partitions/<workspace>/`). Cookies and tokens written by Chromium are encrypted at rest via Electron `safeStorage`, bound directly to your host operating system credential keyring (`libsecret` / GNOME Keyring / KWallet on Linux, Keychain on macOS, DPAPI on Windows).

You can inspect, back up, or erase any of these files directly using standard Linux file utilities at any time.

---

## Workspace Container Isolation

Fly implements multi-account container partitions using dedicated Electron session partitions (`persist:<workspace>`):

* **Cookie Separation:** Cookies, cache, local storage, and IndexedDB data are strictly confined within each workspace partition.
* **Zero Cross-Contamination:** A tracker or cookie set in Workspace 1 cannot inspect or correlate sessions in Workspace 2.
* **Surgical Session Flushes:** Closing tabs flushes memory stores to disk cleanly without sharing state across profiles.

---

## Volatile Private Browsing

Pressing **Ctrl + Shift + P** activates Private Browsing Mode:

* Operates in an isolated, volatile in-memory session partition (`FlyPrivateProfile`).
* Leaves zero disk traces: no history entries, cookies, or cache files are written to storage.
* Terminating private mode or quitting the application instantly wipes the volatile partition from memory.

---

## Ad Blocking & Browser Identity

* **uBlock Origin:** The included extension filters ads and trackers locally in selected persistent workspaces. Its popup and options control exceptions and filter lists. Disabling extensions or uBlock leaves those workspaces without ad blocking.
* **Private browsing:** Electron does not load extensions in the in-memory private session, so private browsing has no ad blocker.
* **Page APIs:** Fly leaves Canvas, WebGL, audio, hardware, language and time zone APIs intact. It does not claim fingerprint anonymity. Pages receive Do Not Track and Global Privacy Control preferences.
* **Identity:** Fly removes application and Electron tokens from its default User-Agent; Google's sign-in page receives Electron's original identity for compatibility. Native header adjustments yield to network extensions.
* **Trusted domains:** The saved list controls permission exceptions, independently of uBlock's site allowlist.

---

## Credential & Password Management

Fly Browser intentionally excludes built-in password saving, autofill prompts, and credential vaults:

* **Elimination of Malware Targets:** Browser-bundled password vaults represent the primary target for infostealers and credential extraction malware. By omitting internal password storage entirely, Fly removes this high-value vulnerability surface.
* **User Autonomy & External Tools:** Users retain full discretion to employ their preferred dedicated password managers (such as Pass, KeePassXC, Bitwarden, or system keychains) outside the browser's execution perimeter.

---

## Policy on Future Diagnostics and Error Reporting

Should diagnostic or crash-reporting mechanisms be added in future versions of Fly, they will adhere strictly to the following guarantees:

1. **Strictly Opt-In:** Telemetry or crash reporting will always be disabled by default. It will never be activated without explicit, affirmative user opt-in.
2. **Strict Anonymisation:** Diagnostic payloads will be limited strictly to application-level stack traces and runtime error codes.
3. **No PII or URLs:** Crash reports will never contain browsed URLs, page contents, cookie headers, IP addresses, or personal identifiers.
4. **Local Inspection:** Any diagnostic log will be stored locally first, allowing users to inspect the exact payload before choosing to submit it.
5. **Clear Controls:** A prominent toggle in Preferences will permit enabling or disabling diagnostics at any moment.

---

## Data Deletion and User Rights

You maintain complete ownership of your data:

* **Purge History:** Press **Ctrl + Shift + H** to open the Actionable History overlay and delete individual entries or clear all history.
* **Surgical Cookie Clear:** Use Preferences (**Ctrl + H**) to purge cookies for the active domain without affecting unrelated workspaces.
* **Full Reset:** Deleting `~/.config/fly-browser/` completely resets the browser to factory defaults.

---

## Open Source Verification

Fly Browser is open source under the GNU General Public Licence v3.0. You are encouraged to inspect, audit, and build the source code directly from GitHub:

* Repository: [https://github.com/Arzuparreta/fly-browser](https://github.com/Arzuparreta/fly-browser)
