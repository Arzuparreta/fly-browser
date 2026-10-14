# Installation & Setup

Mise is designed to be efficient, modular, and easy to run across Linux, macOS, and Windows. You can download pre-built self-contained binaries directly from GitHub Releases or run from source.

---

## Pre-Built Executables

Official releases and cryptographic checksums are published for every tagged version on the [GitHub Releases page](https://github.com/Rakosn1cek/Mise-browser/releases).

### Linux (Distro-Agnostic AppImage)

The AppImage runs out of the box on Ubuntu, Debian, Fedora, Arch Linux, openSUSE, and any standard modern Linux distribution without package managers or root privileges.

1. Download the latest `Mise-<version>-x86_64.AppImage`.
2. Make the file executable:
   ```bash
   chmod +x Mise-*.AppImage
   ```
3. Run the AppImage directly:
   ```bash
   ./Mise-*.AppImage
   ```

The AppImage uses the static AppImage runtime, so it does not need `libfuse2`. It only requires FUSE, which almost every desktop distribution provides through `fuse3`. On systems without FUSE (such as some containers), run it with `--appimage-extract-and-run` instead.

To integrate with desktop menus, you can also place the AppImage in your `~/Applications/` directory or use utilities such as `AppImageLauncher` (version 3 or newer).

### macOS (DMG & Zip)

1. Download the appropriate image for your Mac architecture:
   - Apple Silicon (M1 / M2 / M3 / M4): `Mise-<version>-arm64.dmg`
   - Intel processors: `Mise-<version>-x64.dmg`
2. Open the `.dmg` file and drag **Mise Browser** into your **Applications** folder.
3. **First-Launch Note**: Because independent open-source projects do not carry paid enterprise code-signing certificates, macOS Gatekeeper may present an unverified developer dialog. Right-click **Mise Browser** in Finder and select **Open**, or run the following command in Terminal:
   ```bash
   xattr -cr "/Applications/Mise Browser.app"
   ```

### Windows (Portable & Setup Installer)

1. Download either release artefact:
   - **Portable**: `Mise-<version>-portable.exe` (run directly from any folder, zero installation).
   - **Setup Installer**: `Mise-Setup-<version>.exe` (installs to `%LOCALAPPDATA%` with a Start Menu entry).
2. **First-Launch Note**: Windows SmartScreen may present a *"Windows protected your PC"* banner on freshly released binaries. Click **More info** followed by **Run anyway**.

---

## Verifying Checksums

Every release includes a published `SHA256SUMS.txt` file containing cryptographic hashes for all released packages.

To verify download authenticity:

```bash
sha256sum -c SHA256SUMS.txt
```

Verify that your downloaded file outputs `OK`.

---

## Updating Mise

Mise includes an integrated, zero-privilege update notification system that checks the official GitHub Release ledger:

- **Automated Check**: Mise queries the GitHub Releases API shortly after startup. When a newer version is published, an update badge appears in the sidebar and opens **Preferences → About**.
- **Manual Check**: Run **Check for Updates** in the Command Centre (`Ctrl+P`) or click **Check for updates** in **Preferences → About** (`Ctrl+H`).
- **Applying Updates**:
  - **Linux AppImage**: Download the latest `.AppImage` from GitHub Releases and replace your existing file.
  - **macOS / Windows**: Download and run the latest installer or portable executable.
  - **Source Installs**: Run `git pull && npm install` in your repository folder.

---

## Running from Source

If you prefer building and executing directly from source on Linux:

### Prerequisites

Ensure your system has `Node.js` (v20 or newer) and `npm` installed.

On Arch Linux:
```bash
sudo pacman -S nodejs npm
```

Ensure you have a terminal emulator installed for the clipboard-mediated terminal features (such as `kitty`, `alacritty`, `foot`, `st`, or `xterm`).

### Setup & Launch

1. Clone the repository and navigate into the project directory:
   ```bash
   git clone https://github.com/Rakosn1cek/Mise-browser.git
   cd Mise-browser
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Launch Mise Browser:
   ```bash
   npm start
   ```

Alternatively, you can run the provided launcher script:
```bash
chmod +x launch.sh
./launch.sh
```

---

## Desktop Environment Integration (Source Installs)

To integrate a source install directly into your desktop application launcher (such as Rofi, Wofi, or your desktop application menu) and deploy icons:

```bash
npm run install-desktop
```

This installs `mise.desktop` into `~/.local/share/applications/` and deploys icons across 48x48, 128x128, and 256x256 resolutions to `~/.local/share/icons/hicolor/`.

---

## Configuration Location

All runtime configuration and persistent session data are stored locally in standard Linux XDG format:

```text
~/.config/mise-browser/
├── config.json          # Core engine and preference settings
├── keybinds.json        # Customisable keyboard shortcuts map
├── session.json         # Workspaces, open tabs, titles, and scroll offsets
├── bookmarks.json       # Saved browser bookmarks
├── quickmarks.json      # Single-key jump assignments
├── history.json         # Actionable history entries
└── notes.md             # Integrated markdown notes
```
