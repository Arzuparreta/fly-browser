# Installation & Setup

Fly is designed to be efficient, modular, and easy to run across Linux, macOS, and Windows. You can download pre-built self-contained binaries directly from GitHub Releases or run from source.

---

## Pre-Built Executables

Official releases and cryptographic checksums are published for every tagged version on the [GitHub Releases page](https://github.com/Arzuparreta/fly-browser/releases).

### Installer script (any distribution)

The quickest way to install Fly on Linux. Paste this into a terminal:

```bash
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

The same `fly-install` command then finds every copy of Fly on the machine, updates them and uninstalls them:

```bash
fly-install status       # every installation found, its version, and whether it is current
fly-install update       # brings every installation to the latest release
fly-install uninstall package|PATH|all
```

If your shell does not find `fly-install`, `~/.local/bin` is not in your `PATH`: run it as `~/.local/bin/fly-install`, or add that folder to `PATH` in your shell's startup file.

- On Arch-based systems, `install` and `update` build `fly-browser-bin` from the release's `PKGBUILD` in a temporary directory, which they remove afterwards, so downloads do not pile up. `--appimage` installs the AppImage there instead.
- Elsewhere, the AppImage goes into `~/Applications`, checked against the release's `SHA256SUMS.txt`.
- An AppImage is added to the applications menu in the standard XDG locations that every Linux desktop and launcher reads: the desktop entry goes in `~/.local/share/applications`, and the icons go in the `hicolor` theme. Fly recognises that entry as its own, keeps it pointing at the AppImage, and can remove it from Preferences.
- `status` looks in `~/Applications`, `~/.local/bin`, `~/bin`, `~/Desktop`, `~/Downloads` and the AppImage the menu starts, so stray copies show up. `update` brings each of them up to date.
- `uninstall` removes the package with pacman, or deletes an AppImage together with the menu entry and icons that were added for it. Your profile in `~/.config/fly-browser` is kept.

### Arch Linux and derivatives

On Arch Linux, CachyOS, EndeavourOS, Manjaro and other Arch-based systems, install the `fly-browser-bin` package. Every stable release carries its `PKGBUILD`:

```bash
mkdir fly-browser-bin && cd fly-browser-bin
curl -LO https://github.com/Arzuparreta/fly-browser/releases/latest/download/PKGBUILD
makepkg -si
```

The package installs Fly to `/opt/fly-browser`, the `fly-browser` command, a desktop entry and icons, so Fly appears in Rofi, Wofi, Fuzzel and every desktop menu. When Fly announces a new release, run the same commands again.

`fly-browser-bin` is also going to the AUR, where it will update with your system (`paru -Syu`). AUR account registration is paused at the moment; once the package is published there, `paru -S fly-browser-bin` replaces the commands above, and an installation made with them carries over as it is.

Flags you want on every launch, such as `--ozone-platform=wayland`, go in `~/.config/fly-browser-flags.conf`, one or more per line; `#` starts a comment.

### Linux (Distro-Agnostic AppImage)

The AppImage runs out of the box on Ubuntu, Debian, Fedora, Arch Linux, openSUSE, and any standard modern Linux distribution without package managers or root privileges.

1. Download the latest `Fly-<version>-x86_64.AppImage`.
2. Make the file executable:
   ```bash
   chmod +x Fly-*.AppImage
   ```
3. Run the AppImage directly:
   ```bash
   ./Fly-*.AppImage
   ```

The AppImage uses the static AppImage runtime, so it does not need `libfuse2`. It only requires FUSE, which almost every desktop distribution provides through `fuse3`. On systems without FUSE (such as some containers), run it with `--appimage-extract-and-run` instead.

The first time the AppImage starts, Fly offers to add itself to your applications menu, so it appears in Rofi, Wofi, Fuzzel and your desktop's menu like any installed browser. Fly writes the entry and icons under `~/.local/share` and starts the AppImage through a link, `~/.local/share/fly-browser/fly-browser.AppImage`. When you move the file or replace it with a newer release, the next start re-points the link, so the menu keeps working. Nothing is written if you decline. You can add or remove the entry at any time in **Preferences → About → Desktop integration**. Removing it deletes only what Fly added; the AppImage itself is never touched.

### macOS (DMG & Zip)

1. Download the appropriate image for your Mac architecture:
   - Apple Silicon (M1 / M2 / M3 / M4): `Fly-<version>-arm64.dmg`
   - Intel processors: `Fly-<version>-x64.dmg`
2. Open the `.dmg` file and drag **Fly Browser** into your **Applications** folder.
3. **First-Launch Note**: Because independent open-source projects do not carry paid enterprise code-signing certificates, macOS Gatekeeper may present an unverified developer dialog. Right-click **Fly Browser** in Finder and select **Open**, or run the following command in Terminal:
   ```bash
   xattr -cr "/Applications/Fly Browser.app"
   ```

### Windows (Portable & Setup Installer)

1. Download either release artefact:
   - **Portable**: `Fly-<version>-portable.exe` (run directly from any folder, zero installation).
   - **Setup Installer**: `Fly-Setup-<version>.exe` (installs to `%LOCALAPPDATA%` with a Start Menu entry).
2. **First-Launch Note**: Windows SmartScreen may present a *"Windows protected your PC"* banner on freshly released binaries. Click **More info** followed by **Run anyway**.

---

### Default browser (Linux)

**Preferences → About → Desktop integration → Make default** makes Fly open the links other applications send to the browser. Fly asks `xdg-settings`, which uses your desktop's own setting where it has one. Where it refuses (window managers without a desktop environment, such as i3), Fly writes the same web MIME defaults directly with `xdg-mime`. Both need `xdg-utils`, which almost every desktop installs. When Fly runs from an AppImage that is not in the menu yet, making it the default adds the menu entry first, because the setting refers to it.

---

## Verifying Checksums

Every release includes a published `SHA256SUMS.txt` file containing cryptographic hashes for all released packages.

To verify download authenticity:

```bash
sha256sum -c SHA256SUMS.txt
```

Verify that your downloaded file outputs `OK`.

---

## Updating Fly

Fly includes an integrated, zero-privilege update notification system that checks the official GitHub Release ledger:

- **Automated Check**: Fly queries the GitHub Releases API shortly after startup. When a newer version is published, an update badge appears in the sidebar and opens **Preferences → About**.
- **Manual Check**: Run **Check for Updates** in the Command Centre (`Ctrl+P`) or click **Check for updates** in **Preferences → About** (`Ctrl+H`).
- **Applying Updates**:
  - **Arch package**: Run the `PKGBUILD` commands above again; once it is on the AUR, upgrade with your system (`paru -Syu`).
  - **Linux AppImage**: Click **Update and restart** in the notice. Fly downloads the new AppImage (only the changed blocks, where it can) and checks it against the release's SHA-512. It then replaces the file and restarts into the new version. A file named with its version, such as `Fly-0.16.1-x86_64.AppImage`, is replaced by one carrying the new version's name; a file you renamed keeps its name. The applications menu entry follows the new file. If anything fails, the old AppImage is left as it was and the notice links to the release page instead.
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
   git clone https://github.com/Arzuparreta/fly-browser.git
   cd fly-browser
   ```

2. Launch Fly Browser:
   ```bash
   ./launch.sh
   ```

The launcher installs the dependencies and the Electron runtime on the first run, and again whenever a pull changes them; later starts are immediate. Alternatively, run `npm install` followed by `npm start`.

---

## Desktop Environment Integration (Source Installs)

To integrate a source install directly into your desktop application launcher (such as Rofi, Wofi, or your desktop application menu) and deploy icons:

```bash
npm run install-desktop
```

This installs `fly-browser.desktop` into `~/.local/share/applications/` and deploys icons across 48x48, 128x128, and 256x256 resolutions to `~/.local/share/icons/hicolor/`.

---

## Configuration Location

All runtime configuration and persistent session data are stored locally in standard Linux XDG format:

```text
~/.config/fly-browser/
├── config.json          # Core engine and preference settings
├── keybinds.json        # Customisable keyboard shortcuts map
├── session.json         # Workspaces, open tabs, titles, and scroll offsets
├── bookmarks.json       # Saved browser bookmarks
├── quickmarks.json      # Single-key jump assignments
└── history.json         # Actionable history entries
```

## Startup diagnostics

`launch.sh` stays in the foreground and returns Electron's exit status. Close Fly to return to the prompt; this prevents late warnings being mixed into the next shell command.

If Fly is already running, a new launch prints `Fly is already running; asked its window to come forward.` and exits; with tiling window managers such as i3 that window may be on another workspace, marked urgent rather than focused. When a pull has changed the dependencies, `launch.sh` refuses to reinstall them while a Fly from the same folder is running, because that would delete its files; close it and launch again.

- npm deprecation notices for `inflight`, `rimraf` and `glob` currently come from the development packaging toolchain (`electron-builder`, including its Windows tooling). They are not reports of a leak in a running Fly tab. They remain visible until the upstream dependency chain is replaced; forcing incompatible major versions is not a safe fix.
- npm's installation policy explicitly allows the pinned esbuild setup script and denies the unused Squirrel Windows installer script. Fly's Windows targets are NSIS and portable; Electron's binary is downloaded separately by `install-electron`.
- Electron can print `ExtensionLoadWarning` for uBlock's Manifest V2 and permissions. Fly supplies context menus and navigation APIs through its compatibility layer, but does not claim complete Chrome compatibility. These warnings stay visible. The existing blocking test checks actual network and cosmetic filtering.
- `colorreload-gtk-module` is an optional desktop GTK module. A reference in `~/.config/gtk-3.0/settings.ini` with no installed module produces a GTK warning. Repair the desktop package or remove the stale module entry; it is not a Fly resource.
- Mesa/GBM errors originate in the graphics stack. Check Diagnostics and reproduce with a disposable profile before changing drivers or GPU settings. Do not remove Electron's sandbox or broadly change system library permissions to silence them.
