# Keyboard navigation

Fly is driven from the keyboard. Browser shortcuts (with Ctrl, Alt or a function key) work everywhere, also while you type. Page keys are single keys, as in Vim, and work whenever you are not typing. Every key on this page is a default: change any of them in **Preferences → Keyboard** (**Ctrl + ,**, or **?**). The complete table is in [Keyboard controls](/reference/keybinds).

## The status bar

The bar at the bottom of the window says where the keyboard is and where the page is, laid out like i3bar.

- **Mode block**, at the left edge. `NORMAL` rests on a dark cell. Every other mode is a solid block, so a glance tells you where your keys are going: `INSERT` (light) when they go to a text field, `HINTS` (brass) while link hints are on screen and `PASSTHROUGH` (red) when the page receives everything. Click it to cancel hints, or to enter or leave passthrough.
- **Pending keys.** After the first key of a two-key sequence, such as **g**, that key shows beside the mode until you finish the sequence or it lapses after 0.7 seconds.
- **Reload / stop.**
- **Address line.** The site of the current page in the text colour, followed by the rest of its address in a dimmer one. While you point at a link it shows where the link goes. While a page loads it says **Opening…** (waiting for the site) and then **Loading…**. Short messages, such as **Copied…**, appear here too.
- **Segments**, at the right: extension buttons, web notifications, the position of the current tab (`3/12`), the page zoom (brighter when it is not 100%), the connection (`HTTPS`; `HTTP` and certificate problems in brass or red), the workspace and the clock. Click the zoom, the workspace or the clock to act on them.

**Ctrl + /** shows or hides the bar.

## Modes

1. **NORMAL**: the default. Page keys scroll, switch tabs and open Fly's panels.
2. **INSERT**: entered when a text field takes focus, or with **i**. Keys go to the field. **Escape** leaves the field and returns to NORMAL.
3. **PASSTHROUGH**: for web applications with their own keyboard (documents, terminals, design tools). Every key goes to the page, including browser shortcuts. **Shift + Escape** enters and leaves it.

## Page keys

### Scrolling

| Key | Action |
| :--- | :--- |
| `j` / `k` | Scroll down / up. Tap for 60 CSS pixels, hold to keep moving |
| `h` / `l` | Scroll left / right |
| `d` / `u` | Half a page down / up |
| `g g` / `G` | Top / bottom of the page |

Holding **j/k/h/l** starts moving without waiting for the keyboard repeat delay, and releasing the key stops it. Scrolling follows the container under your last click or activated hint, falling back to the middle of the focused frame, and works inside cross-origin frames.

### Tabs

| Key | Action |
| :--- | :--- |
| `t` | New tab, with the address bar ready |
| `x` | Close the tab |
| `X` | Reopen the tab closed last |
| `J` / `K` | Next / previous tab (down / up the list). Hold to keep going |
| `g t` / `g T` | Next / previous tab, as in Vim |
| `g 0` / `g $` | First / last tab |
| `g a` | The tab you were on before this one (also **Alt + 0**) |
| `< <` / `> >` | Move the tab up / down the list |
| `p` | Pin or unpin the tab |
| `m` / `M` | Mute or unmute the tab / every tab |
| `W` | Move the tab to another workspace |
| `s` | Move the keyboard to the tab list |

### The page

| Key | Action |
| :--- | :--- |
| `o` | Address bar |
| `H` / `L` | Back / forward |
| `g u` / `g U` | Up one level of the address / the first page of the site |
| `r` / `R` | Reload / reload without the cache |
| `f` | Link hints |
| `F` | Link hints that open the link in a background tab |
| `g i` | Put the caret in the first text field on screen |
| `/` | Find in the page |
| `n` / `N` | Next / previous match |
| `y` | Copy the page's address |
| `z i` / `z o` / `z z` | Zoom in / out / back to 100% |
| `e` | Reader view |
| `i` | INSERT mode |

### Fly

| Key | Action |
| :--- | :--- |
| `w` | Workspaces and their tabs |
| `b` | Bookmarks |
| `?` | Keyboard settings: every shortcut, and where to change it |

## Link hints (`f`, `F`)

1. Press **f**. Fly puts a small letter label on every link, button and field on screen, using the keys under your left hand: `q, w, e, a, s, d, z, x, c, r, f, v`. As many labels as possible are a single letter.
2. Type a label. Links open, buttons are pressed and text fields take the caret.
3. **Escape**, or **Alt + F**, closes the labels without choosing. **Backspace** removes a typed letter.

**F** works the same way, but a link you choose opens in a background tab and you stay on the page: use it to open several results from a list. Anything that is not a link is activated as with **f**.

**g i** needs no label: it puts the caret in the first text field on screen, such as a site's search box.

Labels follow their elements when the page moves and hide while an element is off screen. Until you type, a page that is still loading can add labels; once you start typing, the set stays fixed.

## Address bar (`Ctrl + L`, `o`)

**Ctrl + L** opens the address bar with the page's address selected: type over it, or press **Right** to edit it. **Ctrl + T**, **t** and the sidebar's New Tab button open a new tab with the bar ready.

The first row says in words what **Enter** will do with what you typed: **Open address**, or **Search** followed by the site that will search. The rows after it are your open tabs, bookmarks and history that match.

- **Addresses.** Type all or part of one. Fly uses HTTPS, except for machines on your own computer or network (`localhost`, `127.0.0.1:3000`, `192.168.1.1`, `printer.local`, `server:8080`), which it opens over HTTP because they rarely have a certificate. The current page stays on screen until the new one has painted.
- **Open tabs.** A row that starts with **Switch to tab** takes you to a tab that is already open in this window instead of loading the page again.
- **Bookmarks and history** of the workspaces whose window is open. Fly connects to the highlighted result's site while you choose.
- **Search aliases.** `yt ambient` searches YouTube. The built-in ones are below; add your own in **Preferences → General → Search aliases**.
- **Bangs.** Any [DuckDuckGo bang](https://duckduckgo.com/bangs), at the start or the end: `!w Arch Linux`, `Arch Linux !w`.
- **Calculator.** `240 * 1.2`, `15% of 200`, `sqrt(144)`, `2^10`, `sin(pi)`, `log2(256)`. **Enter** copies the result; **Down** or **Tab** puts it in the bar to keep calculating. Nothing leaves your computer.
- **Workspaces.** `ws work` goes to the workspace named Work.
- **Preloading.** With **Preferences → Performance → Preload the address bar's result** on, Fly loads the highlighted address, bookmark or history page in the background so that it appears at once. The site sees that visit even if you do not open it. Searches are never preloaded. It is off by default.

**Escape** closes the bar and returns the keyboard to the page.

### Search aliases

| Alias | Searches |
| :--- | :--- |
| `g` | Google |
| `ddg` | DuckDuckGo |
| `yt` | YouTube |
| `gh` | GitHub |
| `a` | ArchWiki |
| `aur` | Arch User Repository |
| `pkg` | Arch Linux packages |
| `so` | Stack Overflow |
| `r` | Reddit |
| `sp` | Startpage |
| `b` | Brave Search |
| `k` | Kagi |

Without an alias, Fly searches with the engine chosen in **Preferences → General**. Your own aliases can also be written in the configuration file:

![Preferences General section with search aliases](/screenshots/preferences-5.webp)

```json
"custom_search_aliases": {
    "nix": "https://search.nixos.org/packages?query=%s",
    "mdn": "https://developer.mozilla.org/en-US/search?q=%s"
}
```

## Tabs and the sidebar

Tabs are a vertical list in the sidebar. The current tab is a lighter panel with a red bar on its left edge. In a split view the other page on screen has the panel without the bar.

- **Collapsed or pinned.** By default the sidebar is a 36 px strip of icons that opens to 260 px under the pointer, over the page, so the page never changes size. **Ctrl + Shift + Z** or the pin button keeps it open.
- **From the keyboard.** **Alt + S** (or **s**) opens the sidebar and puts the keyboard cursor, four red corner brackets, on the current tab. **j / k** or **Down / Up** move the cursor without switching tabs or waking suspended ones, and **Home / End** (or **G**) jump to the first and last tab. **Enter**, **l** or **Right** selects the tab under the cursor and returns to the page. **x** closes it and keeps the cursor in place. **Escape**, **Alt + S** again or **Alt + W** returns to the page without changing tab. A pinned sidebar stays pinned; an unpinned one closes when the keyboard leaves it. These are the keys you have bound to scrolling and closing, so they follow your own bindings.
- **By number.** **Alt + 1** to **Alt + 8** select the first eight tabs and **Alt + 9** the last one. Hold **Alt** on its own to see the numbers over the tab icons. **Alt + 0** returns to the tab you were on before.
- **Sound.** A speaker on a tab's row means it is playing; click it, or press **m**, to mute it.
- **Closing with the mouse.** The **×** of a row, or a middle click anywhere on it. Pinned tabs have no **×**: unpin them first.
- **The workspace.** The number on a cell at the top of the strip is the workspace of this window; `P` in red is a private window. Click it, or press **w**, for the list of workspaces and tabs.
- **New windows and sign-in popups.** Links that ask for a new window open as tabs. Popups with a size, such as "Continue with Google", open as a small window above Fly in the same workspace so that the sign-in can report back; **Ctrl + W** closes it.

## Workspaces

Each workspace is a separate Chromium profile, with its own cookies, logins and extensions, in its own window. With a tiling window manager such as i3, put each one on its own desktop.

- **Ctrl + 1** to **Ctrl + 9** go to the workspace with that number, opening its window if it is closed.
- **Alt + PageDown** / **Alt + PageUp** go to the next and previous one.
- **w** or **Ctrl + Shift + W** lists every workspace with its number and its tabs. Type to filter, **Enter** to go to a workspace or a tab, and use the buttons of a row to rename or delete a workspace or to move a tab. Drag a tab onto another workspace to move it there.
- **W** moves the current tab to another workspace.
- **Ctrl + Shift + P** opens a private window, which keeps no history or cookies.

See [Workspaces and containers](/guide/workspaces-and-containers).

## Split view

Two tabs of the same window side by side, or one above the other.

- **Ctrl + Shift + S** splits the current tab with the next one, or closes the split.
- **Ctrl + O** moves the keyboard to the other page.
- **Ctrl + \\** swaps the two pages.
- **Stacked**, **Change orientation** and **Close** are in the Command Centre (**Ctrl + P**, type `split`). Give them keys of your own in **Preferences → Keyboard**.
- Right-click a link and choose **Open Link in Split View** to send it to the other page.

Both pages of a split are kept awake while they are on screen.

## Find in page (`/`, `n`, `N`)

**/** or **Ctrl + F** opens the find bar. **Enter** goes to the next match and **Shift + Enter** to the previous one. **Escape** closes the bar.

Back in NORMAL mode, **n** and **N** go to the next and previous match of the last search without opening the bar again. The status bar shows the searched text and the position, such as `news  3/60`, and **Escape** removes the highlights.

## Reader view (`e`, `F9`)

Reader view extracts the article of a page with Mozilla Readability and shows it over the page without reloading it, so closing it (**Escape**, **F9** or its close button) brings the page back as it was. Its controls change the theme (dark, sepia, light), the typeface, the text size (14 to 28 px) and the column width (620, 760 or 940 px). The header estimates the reading time.

## New tab and bookmarks

New tabs open a quiet local page with the Fly illustration. Under it, **Search or enter address** opens the address bar and **Bookmarks** opens the same menu as **Ctrl + Shift + B** (or **b**). The keys shown next to them are your current bindings. The fly stays centred in the available page area. The page follows your theme and interface scale without fetching fonts, favicons or anything else from the network.

Below them, one keyboard tip teaches a key you have not used yet, such as **f** for link hints or **Ctrl + P** for the Command Centre. Each new tab shows the next pending tip. A tip stops appearing once you have used its keys twice, or when you click **×** next to it; when you know them all, the line disappears. Turn the tips off, or show them all again, in **Preferences → Appearance → New tab → Keyboard tips**. Fly counts only those keys, in its `Local State`, and nothing leaves your computer.

Bookmarks are your saved pages, shared by every workspace. Opening one uses the current workspace's profile and cookies. Click a row or press **Enter** to open it: from a new tab it replaces that tab, from a page it opens a new one. **Ctrl + Enter**, Ctrl-click or a middle click opens a background tab and keeps the menu open.

Type in **Search bookmarks** to match names and addresses. **Up/Down** select a result and **Page Up/Page Down** move through long lists.

**Edit**, or **F2**, changes a bookmark's name or address in the same menu. **Add bookmark** (or **Ctrl + N** inside the menu) starts with the current page. **Enter** or **Ctrl + S** saves and **Escape** cancels. Invalid or already saved addresses are explained and your draft is kept.

**Move earlier** / **Move later** in the editor, or **Alt + Up/Down** in the list, set the order. **Delete** in the editor removes the bookmark and **Undo delete** restores it to its place. In the search field, **Delete** keeps its text-editing role and removes the selected bookmark only when the caret is at the end with nothing selected.
