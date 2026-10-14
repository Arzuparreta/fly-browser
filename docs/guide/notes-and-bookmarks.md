# Notes, Bookmarks & Quickmarks

Mise incorporates productivity utilities directly into the browser, saving all data as plain text and JSON files in your local user configuration directory.

---

## Markdown Quick Notes (`Ctrl + N`)

![Mise Browser Markdown Quick Notes](/screenshots/notes.webp)

Mise includes an integrated, expansive scratchpad overlay for taking notes alongside your research:

- Press **Ctrl + N** to open or close the Notes overlay.
- **Expansive Layout & Fullscreen**: Generous, readable dimensions with a dedicated toggle button to expand into a distraction-free full canvas view.
- **Live Statistics**: Real-time word counter and character display in the header.
- **Markdown Preview**: Instant toggle between raw Markdown editing and formatted preview, complete with headings, lists, quotes, code blocks, thematic rules, and clickable links.
- **Instant Persistence**: All notes are automatically saved to:
  ```text
  ~/.config/mise-browser/notes.md
  ```
- **Export from Reader View (`Ctrl + Shift + E`)**: Distilled Reader View articles and web clippings can be appended straight into Quick Notes with a single click or keyboard shortcut.
- **Right-Click Text Clipping**: Highlight any text passage on a web page or in a document and right-click to choose **Save to Notes**. The snippet is formatted into a clean Markdown blockquote with source citation and timestamp, and appended directly to `notes.md`.
- **External Note-Taking Integration**: Click the **External** button in the notes header to open `notes.md` in your preferred desktop note application (Obsidian, Logseq, Neovim, VS Code) or system default Markdown editor. Custom launch commands can be configured in **Preferences → General → Notes & external tools**.

---

## Bookmarks Manager (`Ctrl + Shift + B`)

The Bookmarks overlay allows you to manage saved links with full keyboard navigation:

- Press **Ctrl + Shift + B** to toggle the Bookmarks Manager.
- **Add Bookmark**: Press **Ctrl + D** while viewing any page to save the current URL into your bookmarks collection.
- **Move Between Columns**: Use Left/Right arrows to select quickmarks or bookmarks. Tab moves between controls.
- **Fuzzy Search**: Start typing inside the overlay to filter bookmarks dynamically.
- **Open Bookmark**: Use the arrow keys to highlight a bookmark and press **Enter** to open it.
- **Delete Bookmark**: Highlight a bookmark and press **Delete** to remove it from your collection.
- Bookmarks are stored in:
  ```text
  ~/.config/mise-browser/bookmarks.json
  ```

---

## Quickmarks (Single-Key Fast Jump)

Quickmarks provide instantaneous, single-key bookmarking inspired by Vim and window managers:

### 1. Setting a Quickmark
- While on any page, press **Ctrl + Shift + Q**.
- An indicator prompt asks for a key binding.
- Press any single alphanumeric key (for example, `g` for GitHub or `r` for Reddit).
- The current URL is permanently bound to that key.

### 2. Jumping to a Quickmark
- From anywhere in the browser, press **Ctrl + J**.
- Press the assigned letter key (e.g. `g`).
- Mise instantly navigates your active tab to the bound URL.

Quickmarks are saved in:
```text
~/.config/mise-browser/quickmarks.json
```

---

## Actionable History Overlay (`Ctrl + Shift + H`)

![Mise Browser Actionable History Overlay](/screenshots/history.webp)

- Press **Ctrl + Shift + H** to open the History overlay.
- Search through your browsing history in real time.
- Press **Enter** on any result to open the page.
- Click **Purge History** to wipe all stored history records from `history.json`.
