# Configuration Files

All Mise configurations are stored in standard JSON and Markdown format within your user profile directory:

```text
~/.config/mise-browser/
```

---

## 1. Engine & Preferences (`config.json`)

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
    "webview_theme": "dark",
    "theme_colors": {
        "dark": {
            "accent": "#7aa2f7",
            "bg_main": "#1a1b26",
            "bg_sidebar": "#16161e",
            "text": "#c0caf5",
            "sidebar_opacity": 100,
            "overlay_opacity": 100
        },
        "light": {
            "accent": "#2b59c3",
            "bg_main": "#e5e5e5",
            "bg_sidebar": "#d4d4d4",
            "text": "#1a1a1a",
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

![Performance and Hardware Preferences](/screenshots/preferences-1.webp)

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
- **`webview_theme`**: Website native theme preference (`dark` or `light`), toggled via the sidebar theme button.
- **`theme_colors`**: Custom visual colour palette and opacity settings per theme mode:

![Appearance and Visual Theme Preferences](/screenshots/preferences-2.webp)
  - **`accent`**: Primary highlight colour for active tabs, borders, and controls (defaults to `#7aa2f7` in dark, `#2b59c3` in light).
  - **`bg_main`**: Base window and modal canvas background colour.
  - **`bg_sidebar`**: Vertical sidebar background colour.
  - **`text`**: Primary text and icon foreground colour.
  - **`sidebar_opacity`**: Sidebar background translucency percentage (40% to 100%) with backdrop blur glass effect.
  - **`overlay_opacity`**: Modal and overlay translucency percentage (50% to 100%) for Command Palette, Notes, and Preferences.
- **`trusted_domains`**: Array of domains exempted from tracker blocking and fingerprint spoofing.
- **`external_notes_command`**: Optional command or executable used to open notes and clippings (e.g. `obsidian`, `logseq`, `code`, `nvim`). When empty, uses system default Markdown handler.
- **`custom_search_aliases`**: Key-value map of custom search prefixes and URL templates (with `%s` query placeholder). Typing `<alias> <query>` in the address bar (such as `aur hyprland` or `gh mise`) bypasses fallback engines and routes queries straight to the destination. Users can configure, override, or extend default built-ins directly in Preferences.

---

## 2. Keybindings Map (`keybinds.json`)

Maps internal browser actions to custom keystroke combinations. See the [Keyboard Shortcuts Reference](/reference/keybinds) for the complete list of action names.

---

## 3. Session State (`session.json`, `session.json.bak`, `session_sentinel.json`)

Maintains persistent workspaces, URLs, titles, Dark Reader preferences, and sleeping tab scroll offsets across restarts:

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
    },
    "tab_dark_reader": {
        "Work": [true],
        "Personal": [false]
    }
}
```

### Session Files & Atomic Resilience

- **`session.json`**: The primary session state file. To prevent data corruption during sudden system shutdowns or power cuts, Mise writes updates to a temporary file (`session.json.tmp`) and swaps it via an atomic POSIX rename operation.
- **`session.json.bak`**: A rolling backup journal automatically updated before every primary session flush. If `session.json` is missing or corrupted, Mise automatically recovers the previous state from this journal.
- **`session_sentinel.json`**: A lifecycle sentinel file recording whether the previous browser process terminated cleanly (`"clean_exit": true`). If an abrupt crash, kill signal, or power failure occurs, the sentinel detects `clean_exit: false` on startup, engages crash recovery mode, forces background tabs to sleep to avert memory spikes, and displays a status bar notice.

---

## 4. Bookmarks & Quickmarks

- **`bookmarks.json`**: An array of saved bookmark objects containing `title`, `url`, and creation timestamp.
- **`quickmarks.json`**: A key-value dictionary mapping single alphanumeric keys to URLs (e.g. `"g": "https://github.com"`).

---

## 5. Quick Notes (`notes.md`) & Clippings

- **`notes.md`**: A standard Markdown file storing everything typed into the Quick Notes overlay (**Ctrl + N**) or exported from Reader View (**Ctrl + Shift + E**). You can edit this file directly or open it from the overlay in your favourite external editor (Obsidian, Logseq, Neovim, VS Code).
- **`clippings/`**: Dedicated directory storing standalone exported Markdown clippings and distilled articles saved for external note tools.
