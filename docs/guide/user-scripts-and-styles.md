# Local User Scripts & User Styles

Fly Browser provides built-in support for custom user scripts and user styles, delivering the capabilities of extensions like Greasemonkey, Tampermonkey, and Stylus without the heavy memory footprint of a WebExtension background runtime.

---

## Architecture & Benefits

Traditional browser extensions run continuous background service workers and event loops, consuming hundreds of megabytes of RAM even when idle.

Fly replaces this overhead with a filesystem-driven engine:
* **Zero Extension Runtime**: Scripts and styles are plain text files stored directly in `~/.config/fly-browser/`.
* **Automatic Ingestion**: Fly injects matching `.user.js` and `.user.css` files into `<webview>` instances when the Document Object Model is ready (`dom-ready`).
* **Live Inotify Watching**: Modifications made in your favourite text editor are detected in real time. CSS styles update on the fly without refreshing the page.

---

## Why Fly Avoids Full WebExtensions

Mainstream browsers rely on the WebExtensions API (Manifest V2 and V3). While
flexible, this architecture creates significant problems on low-resource and
fanless Linux hardware:

1. **Heavy Background Memory Overhead**:
   Every installed extension typically spawns its own background service
   worker, isolated storage instance, and inter-process communication bus.
   A browser with five or six extensions routinely wastes 300MB to 800MB of
   system memory before any web page has loaded.

2. **Thermal Strain on Fanless Hardware**:
   Persistent background extension workers frequently wake CPU cores to poll
   events, preventing low-power idle C-states and triggering thermal throttling
   on fanless machines.

3. **Supply-Chain Security Risks**:
   Centralised extension stores suffer from a chronic security problem: popular
   extensions are regularly bought by data-collection companies and updated
   silently with tracking code. Fly enforces a strictly local-only policy.
   Included uBlock updates arrive through Fly releases. Additional scripts, styles
   and extensions come from files you place in your local config directory.

4. **Native Handling of Core Capabilities**:
   When analysing what users actually install extensions for, four use cases
   dominate:
   * **Ad & Tracker Blocking**: Handled by the included uBlock Origin extension.
   * **Tab Suspension**: Handled natively via Fly true tab hibernation.
   * **Custom Styling**: Handled cleanly via local `~/.config/fly-browser/styles/`.
   * **Page Script Tweaks**: Handled cleanly via local `~/.config/fly-browser/scripts/`.

Local `.user.js` and `.user.css` files cover many customisations without another
extension background process. The included uBlock Origin has its own startup
and memory costs, described in the [extension guide](/guide/extensions).

::: info WHEN A USERSCRIPT IS NOT ENOUGH
Fly also supports [Chrome extensions](/guide/extensions) per workspace, unloading them when the workspace goes idle. The included uBlock Origin is enabled by default in new profiles; manually installed extensions run only where selected.
:::

::: tip COMMUNITY SCRIPTS & STYLES
Looking for tested customisations like Amazon Dark Mode, Tokyo Night Wikipedia, or YouTube distraction filters? Explore the [Community Scripts Directory](/guide/community-scripts) and the official [Mise-community-scripts repository](https://github.com/Rakosn1cek/Mise-community-scripts).
:::

---

## Directory Locations

User content files are placed inside their respective configuration folders:

```text
~/.config/fly-browser/scripts/         # Local user scripts (*.user.js or *.js)
~/.config/fly-browser/styles/          # Local user styles (*.user.css or *.css)
~/.config/fly-browser/script-storage/  # Persistent JSON stores for GM_setValue
```

Fly automatically creates these directories and populates template examples on startup. You can also open any folder directly from the Command Palette (**Ctrl + P**):
* `Open User Scripts Directory (~/.config/fly-browser/scripts)`
* `Open User Styles Directory (~/.config/fly-browser/styles)`
* `Open User Script Storage Directory (~/.config/fly-browser/script-storage)`

---

## Writing User Scripts (`.user.js`)

Scripts support standard Greasemonkey/Tampermonkey metadata headers:

```javascript
// ==UserScript==
// @name         GitHub Minimalist
// @version      1.0.0
// @description  Streamline GitHub repository navigation
// @match        https://github.com/*
// @exclude      https://github.com/settings/*
// @connect      api.github.com
// @grant        GM_xmlhttpRequest
// @grant        GM_setValue
// @grant        GM_getValue
// @run-at       document-end
// ==/UserScript==

(function() {
    console.log('Fly user script active on GitHub');
})();
```

### Supported Directives
* `@name`: Identifies the script in browser diagnostics and console logs.
* `@version`: Specifies the script version string.
* `@description`: Summary of what the script does.
* `@author`: Script author name or email.
* `@match`: Specifies URL match patterns (e.g. `https://*.example.com/*` or `*://*/*`).
* `@include`: Additional wildcard patterns or regular expressions.
* `@exclude`: Patterns to skip execution on specific paths or subdomains.
* `@connect`: Declares permitted target hostnames for network requests (e.g. `api.github.com`, `*.example.com`, `self`, or `*`).
* `@grant`: Declares intended API permissions (e.g. `GM_xmlhttpRequest`, `GM_setValue`).
* `@run-at`: Determines execution timing (`document-end`, `document-idle`). In the current engine architecture, all scripts execute upon `dom-ready`, so `document-start` scripts run slightly later once the DOM has initialised.

If no `@match` or `@include` is supplied, the script runs across all standard `http://` and `https://` websites.

### Execution Lifecycle & Scope

1. **DOM-Ready Execution (`@run-at` Timing)**:
   While the `@run-at` header is parsed for compatibility (`document-start`, `document-end`, `document-idle`), the current engine executes scripts when the webview emits `dom-ready`. Consequently, scripts expecting to run at `document-start` (prior to HTML parsing) execute slightly later once the Document Object Model has initialised.

2. **Top-Level Window Scope**:
   User scripts run exclusively within the top-level page window context. Scripts do not inject into child `<iframe>` elements or nested subframes.

3. **Script Execution De-duplication**:
   To prevent scripts from executing multiple times on the same page during rapid redirects, fast re-renders, or racing navigation events, Fly enforces three distinct protection layers:
   * **Navigation Epoch Tracking**: Awaited IPC script queries are validated against a monotonic navigation counter. If the page navigates away before the disk query completes, stale results are discarded immediately.
   * **Per-Webview Script ID Registry**: Injected script identifiers are tracked on the webview instance for the lifetime of the active navigation epoch.
   * **In-Page Execution Guards**: A non-enumerable registry on the guest window prevents duplicate execution of the same userscript wrapper within the active document lifecycle.

---

## Supported Userscript APIs (`GM_*` and `GM.*`)

Fly includes a minimal, secure API shim for popular Greasemonkey and Tampermonkey scripts without needing an extension runtime:

| API | Type | Description |
| :--- | :--- | :--- |
| `GM_xmlhttpRequest(details)` | Network | Cross-origin HTTP request proxied via main process, bypassing webpage CORS. |
| `GM_getValue(key, default)` | Storage | Synchronously reads a stored value from the script's local JSON database. |
| `GM_setValue(key, value)` | Storage | Updates in-memory store immediately and persists to disk in the background. |
| `GM_deleteValue(key)` | Storage | Deletes a stored key from the script's JSON storage file. |
| `GM_listValues()` | Storage | Returns an array of all keys stored for the current script. |
| `GM_addStyle(css)` | DOM | Appends a `<style>` element containing custom CSS to the active document. |
| `GM_registerMenuCommand(name, fn)` | UI | Registers an action callback. |
| `GM_unregisterMenuCommand(id)` | UI | Unregisters a previously added menu action. |
| `GM_setClipboard(text)` | Clipboard | Writes plain text to the system clipboard. |
| `GM_log(...args)` | Debugging | Outputs formatted log entries with script name prefix to DevTools. |
| `GM_openInTab(url)` | Window | Opens a target URL in a new window or tab. |
| `GM_notification(text, title)` | System | Displays a desktop notification. |
| `GM_info` | Metadata | Provides script metadata, version, and execution environment details. |
| `unsafeWindow` | Window | Direct reference to the page window object. |
| `GM.*` (GM4 Promise API) | Promises | Promise-based counterparts (`GM.xmlHttpRequest`, `GM.getValue`, etc.). |

### Cross-Origin Requests (`GM_xmlhttpRequest`)

Standard `fetch()` inside a webpage cannot access third-party endpoints because of browser CORS restrictions. Fly routes `GM_xmlhttpRequest` requests through the privileged main process with strict security boundaries:

* **`@connect` Host Restriction**: Scripts must declare permitted network destinations via `@connect <host>`. A declared root domain covers its subdomains (e.g. `@connect github.com` allows `api.github.com`), while same-origin requests back to the host webpage are always permitted implicitly.
* **SSRF Protection**: To prevent Server-Side Request Forgery against private network infrastructure, requests targeting loopback addresses (`127.0.0.0/8`, `::1`, `localhost`), internal subnets (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`), and link-local metadata endpoints (`169.254.0.0/16`) are unconditionally blocked.
* **DNS Resolution Check & IP Pinning**: Target hostnames are resolved before dispatching requests to prevent DNS rebinding attacks from routing requests to internal private IP addresses. Sockets are pinned directly to the validated IP to eliminate time-of-check race conditions.
* **Redirect Validation**: Every HTTP redirect hop is validated against `@connect` and SSRF rules before following.

```javascript
GM_xmlhttpRequest({
    method: 'GET',
    url: 'https://api.github.com/repos/Arzuparreta/fly-browser/releases/latest',
    headers: {
        'Accept': 'application/vnd.github.v3+json'
    },
    onload: function(response) {
        const data = JSON.parse(response.responseText);
        console.log('Latest Fly release:', data.tag_name);
    },
    onerror: function(err) {
        console.error('Failed to query release info:', err);
    }
});
```

### Isolated Per-Script Storage

Values saved with `GM_setValue` are stored in isolated JSON files under `~/.config/fly-browser/script-storage/`:

```javascript
// Check stored user preference with fallback default
const viewMode = GM_getValue('view_mode', 'compact');

// Save updated preference
GM_setValue('view_mode', 'expanded');
```

---

## Writing User Styles (`.user.css`)

Styles can be authored using either modern UserStyle metadata or classic `@-moz-document` syntax.

### UserStyle Header Format

```css
/* ==UserStyle==
@name           Wikipedia Clean Dark
@match          https://*.wikipedia.org/*
==/UserStyle== */

body {
    background-color: #1a1b26 !important;
    color: #c0caf5 !important;
}
```

### Stylus / `@-moz-document` Format

Fly parses domain and prefix conditions natively, making userstyles from repositories like userstyles.world work out of the box:

```css
@-moz-document domain("reddit.com"), domain("old.reddit.com") {
    .side {
        display: none !important;
    }
}
```

---

## Live Reloading

* When you save changes to any file in `~/.config/fly-browser/styles/`, Fly reloads the stylesheet and hot-swaps the CSS in active tabs immediately.
* When you modify files in `~/.config/fly-browser/scripts/`, the updated JavaScript is parsed and staged for execution on the next page navigation.
* Run **Reload User Scripts and Styles** in the Command Palette (**Ctrl + P**) at any time to force an immediate disk re-scan.
