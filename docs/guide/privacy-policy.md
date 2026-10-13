# Privacy Policy

**Effective Date:** 30 September 2026  
**Applies To:** Mise Browser (Electron Edition)

Mise Browser is designed with a fundamental promise: **your browsing activity belongs exclusively to you, and your data stays on your local machine.**

---

## Zero Telemetry & Zero Profiling

Mise Browser **does not collect, transmit, sell, or monitor any personal data or usage metrics**:

* **No Analytics:** There are no tracking SDKs, telemetry beacons, or metric collectors built into Mise.
* **No Remote Crash Reports:** The browser does not transmit crash traces, memory dumps, or error logs to any remote server.
* **No Profiling or Phone-Home:** The application binary never phones home to track user identity, IP history, or usage statistics. To keep you informed of critical security patches, Mise performs an anonymous HTTPS query directly to the public GitHub Releases API (`api.github.com`) once every 24 hours. This query transmits zero personal identifiers, browsing data, search tokens, or system fingerprints.
* **No Search Interception:** Search queries entered into the floating address bar or Command Palette route directly and exclusively to your chosen search engine (defaulting to DuckDuckGo).

---

## Local Data Storage

All data created during your browsing sessions is stored strictly on your local filesystem under `~/.config/mise-browser/`:

| File | Purpose | Storage Format |
| :--- | :--- | :--- |
| `config.json` | Hardware settings, sleep timeout, trusted domains, split view ratio | Plaintext JSON |
| `session.json` | Workspaces, active tab URLs, titles, hibernate scroll offsets, split view state | Plaintext JSON |
| `history.json` | Browsing history with page titles and visit timestamps | Plaintext JSON |
| `bookmarks.json` | Saved bookmarks and custom folder trees | Plaintext JSON |
| `quickmarks.json` | Single-key speed-dial navigation mappings | Plaintext JSON |
| `notes.md` | Built-in Markdown scratchpad notes | Plaintext Markdown |
| `keybinds.json` | Customised keyboard shortcut mappings | Plaintext JSON |
| `user.css` | Optional user stylesheet overrides | Plaintext CSS |
| `user.js` | Optional user script automation scripts | Plaintext JavaScript |

Persistent Chromium session data, cookies, and local storage caches reside in workspace partition folders (`Partitions/mise_ws_<workspace>/`). Cookies and tokens written by Chromium are encrypted at rest via Electron `safeStorage`, bound directly to your host operating system credential keyring (`libsecret` / GNOME Keyring / KWallet on Linux, Keychain on macOS, DPAPI on Windows).

You can inspect, back up, or erase any of these files directly using standard Linux file utilities at any time.

---

## Workspace Container Isolation

Mise implements multi-account container partitions using dedicated Electron session partitions (`persist:mise_ws_<workspace>`):

* **Cookie Separation:** Cookies, cache, local storage, and IndexedDB data are strictly confined within each workspace partition.
* **Zero Cross-Contamination:** A tracker or cookie set in Workspace 1 cannot inspect or correlate sessions in Workspace 2.
* **Surgical Session Flushes:** Closing tabs flushes memory stores to disk cleanly without sharing state across profiles.

---

## Volatile Private Browsing

Pressing **Ctrl + Shift + P** activates Private Browsing Mode:

* Operates in an isolated, volatile in-memory session partition (`MisePrivateProfile`).
* Leaves zero disk traces: no history entries, cookies, or cache files are written to storage.
* Terminating private mode or quitting the application instantly wipes the volatile partition from memory.

---

## Built-in Shields & Fingerprint Mitigation

Mise actively protects your privacy against third-party web trackers:

* **In-Process Request Blocking:** Network requests are filtered locally using Ghostery's block engine to neutralise marketing trackers, ads, and telemetry scripts before packets leave your machine.
* **Fingerprint Farbling:** Injects subtle, domain-bound mathematical noise into Canvas, WebGL, and Audio API outputs to disrupt passive browser fingerprinting.
* **Identity Standardisation:** Strips internal Electron and Mise identifiers from user-agent headers and Client Hints, presenting a standardised Linux Chromium profile. The one exception is Google's sign-in page (`accounts.google.com`), which receives Electron's own User-Agent, because Google refuses sign-in from the standardised profile.
* **Trusted Domains Bypass:** Explicitly whitelisted domains (e.g. banking portals and payment checkouts) bypass fingerprint farbling to prevent fraud-detection false positives.

---

## Credential & Password Management

Mise Browser intentionally excludes built-in password saving, autofill prompts, and credential vaults:

* **Elimination of Malware Targets:** Browser-bundled password vaults represent the primary target for infostealers and credential extraction malware. By omitting internal password storage entirely, Mise removes this high-value vulnerability surface.
* **User Autonomy & External Tools:** Users retain full discretion to employ their preferred dedicated password managers (such as Pass, KeePassXC, Bitwarden, or system keychains) outside the browser's execution perimeter.

---

## Policy on Future Diagnostics and Error Reporting

Should diagnostic or crash-reporting mechanisms be added in future versions of Mise, they will adhere strictly to the following guarantees:

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
* **Full Reset:** Deleting `~/.config/mise-browser/` completely resets the browser to factory defaults.

---

## Open Source Verification

Mise Browser is open source under the GNU General Public Licence v3.0. You are encouraged to inspect, audit, and build the source code directly from GitHub:

* Repository: [https://github.com/Rakosn1cek/Mise-browser](https://github.com/Rakosn1cek/Mise-browser)
