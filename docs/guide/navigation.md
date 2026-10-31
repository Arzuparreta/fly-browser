# Mouse-Free Navigation

Fly Browser is built around a keyboard-driven workflow, enabling efficient navigation of complex web pages without reaching for a pointing device.

---

## Link Hints Overlay (`f`)

Link Hints allow you to click any link, button, or input field on a web page using short letter combinations:

1. Press **f** in NORMAL mode to trigger the hint labels.
2. Fly injects letter labels over visible interactive elements on the page, using an ergonomic left-hand home-cluster layout: `q, w, e, a, s, d, z, x, c, r, f, v`.
3. Type the label corresponding to your target element (its length depends on the number of targets):
   - Links navigate immediately.
   - Text inputs and search bars automatically receive focus for immediate typing.
4. Press **Escape** or the browser hint shortcut (**Alt + F** by default) to dismiss hints without clicking. Within hints, **f** can be part of a target label.

---

## Floating Address Bar (`Ctrl + L`)

Press **Ctrl + L** at any time to open the floating, high-contrast address overlay. **Ctrl + T** and the sidebar's New Tab button open Quiet, where you can choose search or bookmarks.

- **Direct Navigation**: Type any full or partial URL and press **Enter**. The current page stays on screen until the new one has painted.
- **Loading feedback**: A rotating indicator replaces the tab's favicon as soon as navigation starts, including while waiting for the server. It stays visible in the collapsed sidebar and on background tabs. With the status bar enabled, **Opening…** shows the destination before the new page arrives, followed by **Loading…** while its resources load. The button beside the keyboard mode badge stops the active page's load; when finished, it becomes **Reload page**. Hovering a link still shows its destination in the status bar.
- **Live Search Autocomplete**: As you type, Fly queries the browsing history of the workspaces whose window is open, and your bookmarks. Use the **Up** and **Down** arrow keys to select a suggestion and press **Enter**. Fly connects to the highlighted result's site while you choose.
- **Preloading**: With **Preferences → Performance → Preload the address bar's result** turned on, Fly also loads the page that the highlighted address, history or bookmark result opens, so it appears at once on **Enter**. The site sees that visit even if you do not open it. Searches are never preloaded. It is off by default.
- **Direct Calculator**: Type any arithmetic expression (e.g. `240 * 1.2`, `15% of 200`, `sqrt(144)`) to compute the answer live in the dropdown. Press **Enter** to copy the answer to your clipboard, or press the **Down Arrow** or **Tab** to put the result into the address bar and continue calculating.
- **DuckDuckGo Bangs**: Type any DuckDuckGo bang (e.g. `!w Arch Linux`, `!gh FlyBrowser`, `!yt ambient`) to route directly to thousands of external search destinations.
- **Workspace Switching**: Type `ws` followed by a workspace name (e.g. `ws work`) to bring that workspace's window to the front, or open it, from the address bar.
- Press **Escape** to hide the address bar and return focus directly to the active web page.

---

## DuckDuckGo Bang Redirection

![Preferences General section with search aliases](/screenshots/preferences-5.webp)

Fly supports over 13,500 DuckDuckGo bangs directly from the floating address bar. Bangs route queries directly to the target website:

* `!w Arch Linux`: Redirects directly to the Arch Linux article on Wikipedia.
* `!gh FlyBrowser`: Redirects directly to GitHub repository search.
* `!yt lo-fi`: Redirects directly to YouTube video search.
* `!a NetworkManager`: Redirects directly to the ArchWiki page.
* `!so promises`: Redirects directly to Stack Overflow.

Popular bangs display rich destination badges in the autocomplete dropdown. You can place the bang at the beginning or end of your query (e.g. `!w Arch Linux` or `Arch Linux !w`).

---

## Custom Search Aliases & Shorthand Bangs

In addition to DuckDuckGo bangs, Fly includes native search engine prefixes that execute instantly without redirect delays. Prefix any query with an alias in the floating address bar (**Ctrl + L**):

* `g <query>`: Google
* `ddg <query>`: DuckDuckGo
* `yt <query>`: YouTube
* `gh <query>`: GitHub
* `a <query>`: ArchWiki
* `aur <query>`: Arch User Repository
* `pkg <query>`: Arch Linux Packages
* `so <query>`: Stack Overflow
* `r <query>`: Reddit
* `sp <query>`: Startpage
* `b <query>`: Brave Search
* `k <query>`: Kagi

### User-Customised Search Engines
You can add your own search engines or override existing shortcuts in **Preferences → General → Search aliases** (**Ctrl + H**) or inside `~/.config/fly-browser/config.json`:

```json
"custom_search_aliases": {
    "aur": "https://aur.archlinux.org/packages?K=%s",
    "nix": "https://search.nixos.org/packages?query=%s",
    "mdn": "https://developer.mozilla.org/en-US/search?q=%s"
}
```

Any custom alias is recognised immediately in the address bar and displays an interactive suggestion badge in the autocomplete dropdown.

---

## Live Address Bar Calculator

The address bar includes a built-in mathematical expression evaluator that runs completely locally without external network requests:

* **Basic Arithmetic**: `240 * 1.2`, `(100 + 25) / 5`, `150 - 45`
* **Powers & Roots**: `2^10`, `sqrt(144)`, `cbrt(27)`
* **Percentages**: `15% of 200`, `25% * 80`
* **Trigonometry & Logarithms**: `sin(0)`, `cos(pi)`, `log2(256)`, `log10(1000)`
* **Constants**: `pi`, `e`

Selecting the calculator result copies the computed value to your system clipboard and displays a status bar confirmation.

---

## Search Engine Aliases

When typing queries into the address bar, prepend shorthand prefixes to route searches directly to specific services:

| Shorthand Alias | Search Destination | Example Query |
| :--- | :--- | :--- |
| `g <query>` | Google Search | `g linux kernel archives` |
| `ddg <query>` | DuckDuckGo | `ddg rust async tutorial` |
| `a <query>` | Arch Linux Wiki | `a hyprland config` |
| `pkg <query>` | Arch Linux Package Search | `pkg neovim` |
| `gh <query>` | GitHub Repository Search | `gh chromium` |
| `yt <query>` | YouTube Search | `yt ambient coding music` |
| `r <query>` | Reddit Search | `r archlinux` |
| `so <query>` | StackOverflow | `so javascript closures` |
| `sp <query>` | Startpage Search | `sp private search` |
| `b <query>` | Brave Search | `b web standards` |
| `k <query>` | Kagi Search | `k technical docs` |

If no alias is entered, Fly uses your configured default search engine set in **Preferences → General**. The same section lists your aliases and the built-in ones.

***

## Auto-Collapsing Vertical Sidebar (36px Strip)

To dedicate maximum horizontal screen real estate to web content, Fly features an auto-collapsible vertical sidebar:

* **Compact 36px Strip**: In its default collapsed state, the sidebar shrinks to a 36px icon bar displaying only your workspace badge and crisp tab favicons.
* **Smooth Flyout on Hover**: Moving your mouse pointer over the 36px strip smoothly expands the sidebar to 260px as an overlay flyout with drop shadow. Because this expands as an overlay, the active page never resizes, preventing GPU lag and page reflows on fanless hardware.
* **Shortcut Expansion**: Press **Ctrl + Shift + Z** to pin or unpin the sidebar, using the same state as the pin button. Unpinning clears temporary expansion and sidebar focus; the sidebar shrinks to its 36px strip once the pointer leaves. Press **Alt + S** to expand the unpinned sidebar for keyboard navigation. Pressing **Escape** returns focus to the page.
* **Pinning**: Click the pin icon button at the bottom of the sidebar, toggle **Auto-collapse sidebar** in **Preferences → Appearance** (**Ctrl + H**), or run **Toggle Sidebar Expansion (Pin / Unpin)** in the Command Palette to pin the sidebar permanently open at 260px.

***

## Sidebar and Page Focus Management

Fly maintains clean separation between sidebar navigation and active web page interaction:

- **Alt + S**: Shifts keyboard focus to the vertical tab sidebar, smoothly expanding it. Use the **Up** and **Down** arrow keys (or `j` / `k`) to cycle through open tabs, and press **Enter** to switch to the highlighted tab.
- **Alt + W**: Shifts keyboard focus directly into the active page, automatically collapsing the sidebar back to the 36px strip.
- **Ctrl + W**: Closes the currently active tab.
- **Ctrl + R**: Reloads the active tab.

***

## Modal Navigation Engine

Fly Browser provides a Vim-inspired modal navigation engine that frees you from chorded modifiers during routine web browsing. The engine distinguishes between three primary operational states:

1. **NORMAL Mode**: The default state. Home-row keys control page scrolling and browser actions without requiring Ctrl or Alt chords.
2. **INSERT Mode**: Automatically entered whenever you click or focus a text input, search box, or content-editable field (or press `i` explicitly). Keystrokes pass directly into the field. Pressing **Escape** blurs the field and immediately returns to **NORMAL** mode.
3. **PASSTHROUGH Mode**: Designed for rich web applications like Google Docs, sheets, design suites, and cloud terminals. In passthrough mode, all keystrokes pass directly to the guest web application without browser interception. Toggle passthrough mode at any time using **Shift + Escape** or by clicking the mode badge in the status bar.

### NORMAL Mode Keymap

| Key | Action | Description |
| :--- | :--- | :--- |
| `j` | Scroll Down | Smooth scroll down by 80px |
| `k` | Scroll Up | Smooth scroll up by 80px |
| `d` | Half Page Down | Scrolls down by half the viewport height |
| `u` | Half Page Up | Smooth scroll up by half the viewport height |
| `h` | Scroll Left | Smooth scroll left by 80px |
| `l` | Scroll Right | Smooth scroll right by 80px |
| `gg` | Jump to Top | Smooth scroll to top of page |
| `G` | Jump to Bottom | Smooth scroll to bottom of page |
| `t` | New Tab | Opens a new tab with default search engine |
| `x` | Close Tab | Closes the current active tab |
| `o` | Address Bar | Opens floating address overlay for navigation |
| `r` | Reload | Reloads active tab |
| `R` | Force Reload | Reloads active tab ignoring cache |
| `H` | History Back | Navigates back in page history |
| `L` | History Forward | Navigates forward in page history |
| `/` | Find in Page | Opens the in-page search bar |
| `f` | Link Hints | Injects hint labels over visible interactive elements |
| `e` | Reader View | Toggles native distraction-free reader view |
| `w` | Dashboard | Opens the Workspace Dashboard tree |
| `s` | Focus Sidebar | Moves focus to vertical sidebar tab list |
| `y` | Yank URL | Copies current URL to clipboard with status bar feedback |
| `i` | Insert Mode | Explicitly enters INSERT mode |
| `Shift + Escape` | Passthrough Mode | Toggles PASSTHROUGH mode on or off |

***

## Workspace Badges, Tab Selection & Favicons

* **Selecting Tabs by Position**: Press **Alt + 1** through **Alt + 8** to select tabs 1 through 8 from anywhere, or the last tab when fewer exist. **Alt + 9** always selects the last tab. To use another modifier for all nine, set it in **Tab numbers** in **Preferences → Keyboard**. Hold the left **Alt** key (or your chosen modifier) on its own to see these numbers painted over the tab icons: 1 to 8 on the first eight tabs and 9 on the last one. They disappear when you release Alt or press any other key.
* **Non-Disruptive Sidebar Cursor Navigation**: Moving with **j** / **k** or **Down** / **Up** moves the cursor highlight across tabs without activating pages or loading tabs restored on demand. Press **Enter**, **Space**, or **l** / **Right** to activate the highlighted tab.
* **Workspace Badges**: The top of the strip displays a compact letter or number badge for the current workspace (such as `W1` or `P`). Clicking the workspace badge opens the Workspace Dashboard (**Ctrl + Shift + W**). It shares the top row of the sidebar with the Back, Forward, New Tab and Split View buttons; hover it to see the workspace's full name.
* **Favicons & Indicators**: Each tab renders its website favicon with smart local and remote caching. Audio and mute icons control sound; pin and split-pane indicators identify those controls. Suspended pages have no extra badge.
* **Closing Tabs with the Mouse**: Hover a tab in the expanded sidebar and click its **×** button, or middle-click the tab, to close it without selecting it first. The selected tab always shows its **×**. Pinned tabs have no close button and ignore middle-click; unpin them first.
* **New Windows and Sign-In Popups**: Links that open a new window, and plain `window.open` calls, open as tabs in the current workspace. Popups a page opens with a size, such as "Continue with Google", open as a small window above Fly in the same workspace container, so the sign-in can report back to the page; **Ctrl + W** closes it. A page that closes itself, as many sign-in callbacks do, closes its tab; a pinned tab reloads instead.

***

## Dual Split View (1x2 Side by Side and Stacked)

For comparing documentation, pull requests, or research sources without tiling window manager complexity, Fly includes a built-in dual split view:

* **Instant Comparison**: Press **Ctrl + \\** or **Ctrl + Alt + S** (or click the column icon in the sidebar) to split the viewport into two 50% columns side by side.
* **Orientation Toggle**: Right-click the split icon button or execute **Cycle Split View Orientation** in the Command Palette to switch between side by side (vertical columns) and stacked (horizontal rows).
* **Focus Switching**: Press **Ctrl + Alt + O** or **Ctrl + Alt + Tab** to switch keyboard focus between Pane 1 and Pane 2. The active pane displays an accent focus ring, and its matching sidebar tab is highlighted with a numbered badge (1 or 2).
* **Pane Swapping**: Press **Ctrl + Alt + X** to swap the positions of the two open split panes instantly.
* **Context Menu Link Routing**: Right-click any hyperlink on a page and select **Open Link in Split View** to open or send that link directly into the adjacent comparison pane.
* **Tab Assignment**: Clicking any tab in the sidebar while in split mode routes that page into whichever pane currently holds active focus.
* **Suspension Exemption**: Both visible split panes are automatically protected from background suspension while displayed on screen.

***

## Native Reader View (`F9` / `e`)

Fly includes an in-engine DOM distiller powered by Mozilla Readability to strip ads, sidebars, cookie banners, and visual clutter from articles:

* **Instant Distillation**: Press **F9** or press **e** in NORMAL mode to extract the core article, byline, and imagery into a clean reading format.
* **Isolated Shadow DOM**: The reader view is rendered inside an isolated Shadow DOM container over the active document. Closing Reader View (via **Escape**, **F9**, or the close icon) restores the original web page instantly without reloading or discarding form inputs.
* **Custom Typography and Themes**: A floating controls pill at the top-right corner allows adjusting:
  * **Themes**: Dark (Tokyo Night), Sepia (Warm Paper), and Light.
  * **Font Families**: Modern Sans-serif, Classic Serif, or Clean Monospace.
  * **Font Size**: Adjustable scale between 14px and 28px.
  * **Reading Width**: Narrow (620px), Medium (760px), or Wide (940px).
* **Reading Metrics**: Header estimates word count and reading time based on typical reading velocity.


## Quiet new tab and bookmarks

New tabs open a quiet local page with the Fly illustration. **Search or enter address** opens Fly's native address overlay, with your configured search engine, aliases, bangs and calculator. **Bookmarks** opens the same menu as **Ctrl + Shift + B**. The hints reflect your current bindings. Both controls work by mouse, Tab and Enter. The page follows your theme and interface scale without fetching fonts, favicons or other network content. It does not display history suggestions or sample memory use; memory information remains in Diagnostics and the clock remains in the status bar.

Bookmarks are your explicit saved pages, shared across workspaces. Opening a bookmark uses the current workspace's profile, cookies and private-mode identity. Click a row or press Enter to open it. From a new tab, the destination replaces that empty tab; from an existing page, it opens in a new tab. **Ctrl + Enter**, Ctrl-click or middle-click opens a background tab while keeping the menu available for opening several pages.

Type in **Search bookmarks** to match names and addresses. Up/Down selects a result; Page Up/Page Down moves through longer lists. Full names and addresses remain available to accessibility tools and in tooltips when the visible text is shortened.

Choose **Edit** beside a bookmark or press **F2** to change its name or address in the same menu. **Add bookmark** (or **Ctrl + N** inside the menu) starts with the current page's details when available. Enter or Ctrl + S saves the fields; Escape cancels the draft. Invalid or already saved addresses show an explanation and keep your draft. Tab and Shift + Tab move through the editor without triggering page navigation keys.

Use **Move earlier** / **Move later** in the editor or **Alt + Up/Down** in the list to choose a stable order for daily destinations. Order changes in the editor are saved with the name and address; Escape cancels the whole draft. Alt + Up/Down in the list reorders immediately. **Delete** in the editor removes the bookmark; **Undo delete** restores it to its previous position. Delete in the search field keeps normal text-editing behavior, and removes the selected bookmark only when the caret is at the end with no text selected. Escape closes the menu and restores focus to the page.
