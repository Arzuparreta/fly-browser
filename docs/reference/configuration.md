# Configuration Files

All Fly configurations are stored in standard JSON and Markdown format within your user profile directory:

```text
~/.config/fly-browser/
```

---

## 1. Engine & Preferences (`config.json`)

Use **Settings → Appearance → Interface scale** to choose **Normal (100%)** or **Large (125%)**. Large enlarges text, icons and controls immediately and is remembered across restarts. Website zoom is independent of interface scale and is shared by HTTP/HTTPS tabs with the same hostname, across workspaces and restarts. Subdomains have separate preferences; schemes and ports share them. This uses native rendering rather than stretching a bitmap, and adds no background work. Native operating-system window decorations and menus still follow system settings.

The `interface_scale` preference accepts `"normal"` (default) or `"large"`; unknown values fall back to Normal.

Controls hardware switches, process limits, privacy whitelists, and search preferences:

```json
{
    "disable_gpu": false,
    "background_throttling": true,
    "process_limit": 1,
    "tab_sleep_timeout_minutes": 15,
    "sidebar_auto_collapse": true,
    "show_status_bar": true,
    "show_window_controls": false,
    "email_handler": "system",
    "search_engine": "https://duckduckgo.com/?q=%s",
    "custom_search_aliases": {
        "aur": "https://aur.archlinux.org/packages?K=%s",
        "nix": "https://search.nixos.org/packages?query=%s",
        "mdn": "https://developer.mozilla.org/en-US/search?q=%s"
    },
    "spellchecker_language": "en-GB",
    "theme": "dark",
    "sync_website_theme": true,
    "theme_colors": {
        "dark": {
            "accent": "#e0261c",
            "bg_main": "#121211",
            "bg_sidebar": "#0b0b0a",
            "text": "#e7e1d3",
            "sidebar_opacity": 100,
            "overlay_opacity": 100
        },
        "light": {
            "accent": "#e0261c",
            "bg_main": "#ece6d8",
            "bg_sidebar": "#e3dccb",
            "text": "#1a1814",
            "sidebar_opacity": 100,
            "overlay_opacity": 100
        }
    },
    "trusted_domains": [
        "x.com",
        "amazon.co.uk",
        "accounts.google.com",
        "google.com"
    ]
}
```

### Options Description

![Preferences Performance section](/screenshots/preferences-1.webp)

- **`disable_gpu`**: Set to `true` to disable hardware GPU acceleration on systems with problematic graphics drivers.
- **`background_throttling`**: Throttles timers and delays tasks in background tabs to conserve CPU cycles.
- **`process_limit`**: Hard cap on Chromium renderer process forks (1 to 5).
- **`tab_sleep_timeout_minutes`**: Inactivity timeout before background tabs detach and hibernate (5, 15, 30, 60, or 0 to disable).
- **`sidebar_auto_collapse`**: When `true`, collapses the vertical sidebar into a 36px icon strip, expanding smoothly on hover or via shortcut.
- **`show_status_bar`**: When `true`, displays the 22px footer status bar with modal state, real-time link hover preview, and TLS security status.
- **`show_window_controls`**: When `true`, displays window action buttons (minimise, maximise/restore, close) at the top of the sidebar for desktop environments without native window decorations.
- **`email_handler`**: Choose between `system` (OS default `mailto:`) or webmail providers (Gmail, Zoho, Outlook, Fastmail, ProtonMail).
- **`search_engine`**: Fallback search provider URL when non-URL queries are entered without an alias.
- **`spellchecker_language`**: Dictionary language used for input spellchecking (e.g. `en-GB`, `en-US`, `en-CA`, `en-AU`, `cs`, `de`, `fr`, `es`, `it`, `pt`, `nl`, `pl`, or `disabled`).
- **`theme`**: Interface theme preference (`dark` for Tokyo Night or `light`).
- **`sync_website_theme`**: When `true` (the default), websites that offer their own dark mode follow the interface theme through the standard `prefers-color-scheme` preference, updating live. Pages without one are never altered. When `false`, websites always see the light preference.
- **`theme_colors`**: Custom visual colour palette and opacity settings per theme mode:

![Preferences Appearance section](/screenshots/preferences-2.webp)
  - **`accent`**: Primary highlight colour for active tabs, borders, and controls (defaults to `#e0261c` in dark, `#e0261c` in light).
  - **`bg_main`**: Base window and modal canvas background colour.
  - **`bg_sidebar`**: Vertical sidebar background colour.
  - **`text`**: Primary text and icon foreground colour.
  - **`sidebar_opacity`**: Sidebar background translucency percentage (40% to 100%) without backdrop blur.
  - **`overlay_opacity`**: Modal and overlay translucency percentage (50% to 100%) for Command Palette, Notes, and Preferences.
- **`trusted_domains`**: Array of domains allowed restricted site permissions, including subdomains. Independent of uBlock Origin’s allowlist.
- **`extensions_enabled`**: Master switch for [Chrome extensions](/guide/extensions). `true` by default so the included uBlock Origin can block ads. A saved `false` is preserved on upgrade; while `false` no extension runs and there is no ad blocking.
- **`extensions_by_workspace`**: Map of workspace name to the extension folders (inside `~/.config/fly-browser/extensions/`) allowed to run there, e.g. `{ "Work": ["darkreader"] }`. Workspaces without a saved selection use the included extensions; an explicit empty selection loads none.
- **`extensions_unload_grace_seconds`**: How long a workspace keeps its extensions loaded after its last live tab closes or hibernates (default `60`).
- **`custom_search_aliases`**: Key-value map of custom search prefixes and URL templates (with `%s` query placeholder). Typing `<alias> <query>` in the address bar (such as `aur hyprland` or `gh mise`) bypasses fallback engines and routes queries straight to the destination. Users can configure, override, or extend default built-ins directly in Preferences.

---

## 2. Keybindings Map (`keybinds.json`)

Version 2 stores separate `shortcuts` and `normal` tables for browser chords and Vim-style page keys. Use **Preferences → Keyboard** to record, validate and save bindings without editing the file. See the [Keyboard Shortcuts Reference](/reference/keybinds) for the complete list of action names.

---

## 3. Session State (`session.json`, `session.json.bak`, `session_sentinel.json`)

Maintains persistent workspaces, URLs, titles and sleeping tab scroll offsets across restarts:

```json
{
    "current_workspace": "Work",
    "workspaces": {
        "Work": [
            "https://github.com/notifications"
        ],
        "Personal": [
            "https://news.ycombinator.com"
        ]
    },
    "tab_titles": {
        "Work": ["Notifications"],
        "Personal": ["Hacker News"]
    },
    "tab_sleep_states": {
        "Personal": [
            {
                "url": "https://news.ycombinator.com",
                "title": "Hacker News",
                "scrollX": 0,
                "scrollY": 840,
                "hibernatedAt": 1727299000000
            }
        ]
    }
}
```

### Session Files & Atomic Resilience

- **`session.json`**: The primary session state file. To prevent data corruption during sudden system shutdowns or power cuts, Fly writes updates to a temporary file (`session.json.tmp`) and swaps it via an atomic POSIX rename operation.
- **`session.json.bak`**: A rolling backup journal automatically updated before every primary session flush. If `session.json` is missing or corrupted, Fly automatically recovers the previous state from this journal.
- **`session_sentinel.json`**: A lifecycle sentinel file recording whether the previous browser process terminated cleanly (`"clean_exit": true`). If an abrupt crash, kill signal, or power failure occurs, the sentinel detects `clean_exit: false` on startup, engages crash recovery mode, forces background tabs to sleep to avert memory spikes, and displays a status bar notice.

---

## 4. Bookmarks & Quickmarks

- **`bookmarks.json`**: An array of saved bookmark objects containing `title`, `url`, and creation timestamp.
- **`quickmarks.json`**: A key-value dictionary mapping single alphanumeric keys to URLs (e.g. `"g": "https://github.com"`).


## 5. Website Zoom (`site-zoom.json`)

Stores HTTP/HTTPS zoom levels by hostname. Reset Site Zoom Level restores 100% in every open normal tab on that hostname and removes its saved preference. Private tabs share separate zoom preferences held only in memory for the current run; they neither read nor change saved website zoom. Reset in a private tab affects only private tabs. Internal and local pages do not save zoom preferences.
