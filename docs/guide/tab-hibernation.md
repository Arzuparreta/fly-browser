# True Tab Hibernation (Sleeping Tabs)

True Tab Hibernation is a core performance feature in Mise Browser designed to cap Chromium memory and process consumption on fanless and low-resource hardware.

---

## Why True Hibernation Matters

Standard browsers keep background tabs mounted in the DOM. Even when background timers are throttled, each open tab maintains an active Chromium renderer process consuming between 100 MB and 400 MB of RAM, in addition to background cache allocations. On fanless devices, 15 open tabs can quickly consume 3 GB of memory and induce CPU thermal throttling.

Mise resolves this by implementing **True Hibernation**:
When an inactive tab exceeds its sleep timer, its `<webview>` element is completely detached from the DOM. This terminates the guest document and JavaScript execution context, slashing RAM consumption by up to 85% down to the baseline Chromium process overhead (around 40-90 MB) while keeping the tab visible in the sidebar.

---

## What Gets Preserved

Before detaching an idle tab, Mise queries the live page to save its complete state:

- **Live URL**: Preserves the exact current URL, including in-page hash changes and deep links.
- **Page Title**: Retains the human-readable document title in `~/.config/mise-browser/session.json`.
- **Exact Scroll Coordinates**: Captures `[window.scrollX, window.scrollY]` offsets so that long articles, documentation pages, and feeds reopen at the precise reading position.

---

## Audio & Media Protection

Tabs currently playing audio or video are strictly protected:

- If you are playing music, a podcast, or a video on YouTube or Spotify in a background tab, Mise detects the active media stream via `media-started-playing` and `isCurrentlyAudible()`.
- Audible tabs are exempted from sleep scans and will never be hibernated while playing.

---

## Re-hydration on Demand

Waking a dormant tab is instantaneous:

1. Click the sleeping tab in the sidebar or select it using keyboard navigation.
2. Mise re-instantiates the `<webview>` element inside the appropriate workspace container partition.
3. Upon page load, the saved scroll position is automatically restored.
4. The sleeping indicator clears, and the tab becomes fully active.

---

## Cold Boot Hibernation

When you launch Mise Browser with dozens of tabs across multiple workspaces:

- Only the single active tab in your current workspace is mounted into memory on startup.
- All background tabs initialise in dormant sleep state, displaying their saved titles and sleep badges in the sidebar.
- The browser opens in seconds and uses minimal initial RAM.

---

## Crash & Unexpected Shutdown Recovery

Hibernation handles memory pressure gracefully during normal browsing, but Mise also includes comprehensive crash resilience to protect your work if the system loses power, the Linux kernel invokes the Out-Of-Memory (OOM) killer, or Electron is force-quit:

### 1. Atomic Disk Writes & Rolling Journal
Sessions are never saved via naive synchronous file overwrites. When writing to disk:
- Mise first writes the complete session payload to a temporary file (`session.json.tmp`).
- The existing valid session file is rotated into a rolling backup journal (`session.json.bak`).
- Mise performs an atomic POSIX rename (`session.json.tmp` to `session.json`).

Because the rename operation is atomic at the filesystem level, power failures or process terminations can never produce a corrupted or half-written session file. If the primary file is ever damaged or missing, Mise automatically falls back to `session.json.bak` and restores your workspaces without data loss.

### 2. Clean-Exit Sentinel Tracking
A dedicated lifecycle sentinel (`session_sentinel.json`) tracks whether the browser exited cleanly:
- On startup, Mise marks the sentinel as active (`clean_exit: false`).
- On graceful shutdown (such as closing the window or quitting via shortcut), the sentinel is updated to `clean_exit: true`.
- If Mise launches and detects `clean_exit === false`, it recognises that the previous session ended abruptly and automatically engages crash recovery mode.

### 3. OOM Loop Prevention
When recovering from an unexpected termination, Mise automatically enforces hibernation across **all background tabs in all workspaces**:
- Even if multiple tabs were actively loaded before the crash, only the single active tab of your current workspace is mounted into memory.
- All other tabs load in dormant sleep state with their titles, URLs, favicons, scroll offsets, and Dark Reader flags intact.
- This ensures that recovering a session with dozens of tabs never triggers an immediate secondary memory spike or crash loop on fanless hardware.

### 4. Unobtrusive Notification
Consistent with Mise's uncluttered canvas philosophy, crash recovery does not obstruct your screen with modal dialogues or alert boxes. A calm, non-intrusive status bar notification briefly appears at the bottom of the window:

```text
Session restored after unexpected shutdown
```

---

## Configuring the Sleep Timeout

You can customise how quickly tabs hibernate using either the in-browser Preferences overlay or the native top menu bar:

### Method 1: In-Browser Preferences Overlay (Ctrl + H)

![Mise Browser Performance & Hardware Preferences](/screenshots/preferences-1.webp)

1. Open **Preferences** by pressing **Ctrl + H** (or open the Command Palette with **Ctrl + P** and select **Preferences**).
2. Locate **Tab Hibernation (Sleep Timeout)** under the **Performance & Hardware** section.
3. Choose your preferred idle duration from the dropdown:
   - **5 minutes**: Aggressive memory saving.
   - **15 minutes**: Recommended default for daily workflow.
   - **30 minutes**: Moderate timeout.
   - **1 hour**: Relaxed timeout.
   - **Never (Disabled)**: Keeps all background tabs awake.
The change takes effect immediately without needing to restart the browser.

### Method 2: Native Menu Bar (F1)

![Mise Settings Tab Hibernation Menu](/screenshots/menu.webp)

1. Press **F1** to display the native top menu bar.
2. Click **Mise Settings** -> **Tab Hibernation (Sleep Timeout)**.
3. Select your desired sleep timeout duration.

---

## Manual Hibernation Controls

You can also trigger hibernation manually via the Command Palette (**Ctrl + P**):

- **Hibernate Inactive Tabs**: Immediately detaches all eligible idle background tabs across all workspaces.
- **Wake All Tabs in Workspace**: Re-instantiates all sleeping tabs in the active workspace at once.
