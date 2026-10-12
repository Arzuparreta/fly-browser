# Community Scripts & Styles

A curated catalogue of lightweight user styles (`*.user.css`) and user scripts (`*.user.js`) tested specifically for Mise Browser.

All scripts and styles are maintained in the official **[Mise-community-scripts](https://github.com/Rakosn1cek/Mise-community-scripts)** repository.

---

## Why Community Scripts?

Mise deliberately avoids traditional WebExtension stores to protect your privacy and keep memory usage minimal on low-power and fanless hardware. 

Instead of running heavy background extension workers, Mise uses a filesystem-driven engine:
* **Zero Idle Overhead**: Scripts and styles only execute when matching pages load.
* **Instant Hot-Reloading**: Inotify monitors your local files in real time. Styles apply on the fly without refreshing the page.
* **Full Local Ownership**: You have full transparency over every line of CSS or JavaScript running in your browser.

---

## Quick Installation

Installing a community script or style takes only a few seconds:

### Option 1: One-Line Terminal Download

Download the raw file directly into your local configuration directory:

```bash
# Example: Install Amazon Dark Mode style
curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/styles/amazon-dark/amazon-dark.user.css \
  -o ~/.config/mise-browser/styles/amazon-dark.user.css

# Example: Install Copy Markdown Link script
curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/scripts/copy-markdown-link/copy-markdown-link.user.js \
  -o ~/.config/mise-browser/scripts/copy-markdown-link.user.js
```

### Option 2: Via Command Palette

1. In Mise Browser, press **Ctrl + P** to open the Command Palette.
2. Search for **Open User Styles Directory** or **Open User Scripts Directory**.
3. Save or drop your downloaded `.user.css` or `.user.js` files into the opened folder.

---

## Curated Catalogue

### User Styles (`.user.css`)

#### 1. Amazon Dark Mode

A high-contrast dark theme designed for Amazon UK and Amazon US storefronts, which lack a native dark mode.

* **Target Domains**: `amazon.co.uk`, `amazon.com`
* **Features**:
  * Inverts bright backgrounds across search, product pages, and checkout.
  * Preserves original colours on product photos, videos, and icons.
  * Prevents double inversion on responsive `<picture>` elements.
  * Normalises blend modes to eliminate photo blackout glitches.
* **Repository**: [View on GitHub](https://github.com/Rakosn1cek/Mise-community-scripts/tree/main/styles/amazon-dark)
* **Direct Download**:
  ```bash
  curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/styles/amazon-dark/amazon-dark.user.css -o ~/.config/mise-browser/styles/amazon-dark.user.css
  ```

#### 2. Wikipedia Tokyo Night

A custom stylesheet that styles Wikipedia to match the default Tokyo Night visual theme of Mise Browser.

* **Target Domains**: `wikipedia.org`
* **Features**:
  * Harmonious palette using `#1a1b26` background, `#16161e` navigation, `#7aa2f7` accents, and `#c0caf5` text.
  * Dimmed content imagery with full brightness on hover.
  * Styled syntax highlighting for code and reference tables.
  * Compatible with both Vector 2022 and classic Wikipedia skins.
* **Repository**: [View on GitHub](https://github.com/Rakosn1cek/Mise-community-scripts/tree/main/styles/wikipedia-tokyo-night)
* **Direct Download**:
  ```bash
  curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/styles/wikipedia-tokyo-night/wikipedia-tokyo-night.user.css -o ~/.config/mise-browser/styles/wikipedia-tokyo-night.user.css
  ```

---

### User Scripts (`.user.js`)

#### 1. Copy Markdown Link

A global productivity script that copies the active page title and URL formatted cleanly as Markdown.

* **Target Domains**: All websites (`*://*/*`)
* **Hotkey**: **Alt + C**
* **Features**:
  * Generates clean `[Page Title](URL)` directly on your system clipboard.
  * Automatically ignores keystrokes when typing inside input boxes, text areas, or editable content.
  * Uses native `GM_setClipboard` for seamless clipboard integration.
* **Repository**: [View on GitHub](https://github.com/Rakosn1cek/Mise-community-scripts/tree/main/scripts/copy-markdown-link)
* **Direct Download**:
  ```bash
  curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/scripts/copy-markdown-link/copy-markdown-link.user.js -o ~/.config/mise-browser/scripts/copy-markdown-link.user.js
  ```

#### 2. YouTube Distraction-Free

A minimalist script that removes algorithmic recommendation traps and clutter from YouTube while preserving video playback.

* **Target Domains**: `youtube.com`
* **Features**:
  * Hides algorithmic recommendations on the home feed.
  * Strips YouTube Shorts shelves, guide links, and mini-player entries.
  * Removes the related videos sidebar on playback pages.
  * Keeps search, subscriptions, playlists, and video controls fully functional.
* **Repository**: [View on GitHub](https://github.com/Rakosn1cek/Mise-community-scripts/tree/main/scripts/youtube-distraction-free)
* **Direct Download**:
  ```bash
  curl -fsSL https://raw.githubusercontent.com/Rakosn1cek/Mise-community-scripts/main/scripts/youtube-distraction-free/youtube-distraction-free.user.js -o ~/.config/mise-browser/scripts/youtube-distraction-free.user.js
  ```

---

## Contributing Your Own Customisations

Have you written a custom `.user.css` stylesheet or `.user.js` script that improves your browsing workflow?

Contributions to the community collection are warmly welcome:

1. Fork the **[Mise-community-scripts](https://github.com/Rakosn1cek/Mise-community-scripts)** repository.
2. Add your customisation into `styles/<name>/` or `scripts/<name>/` with a brief `README.md`.
3. Follow the standards outlined in [CONTRIBUTING.md](https://github.com/Rakosn1cek/Mise-community-scripts/blob/main/CONTRIBUTING.md):
   * Pure CSS and vanilla JavaScript only (no external CDNs or heavy frameworks).
   * Strict privacy: zero telemetry, analytics, or remote phone-home requests.
   * Standard metadata header (`@name`, `@match`, `@description`).
4. Submit a Pull Request!
