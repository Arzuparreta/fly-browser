# Bookmarks & History

Fly incorporates productivity utilities directly into the browser, saving all data as plain JSON files in your local user configuration directory.

---

## Bookmarks Manager (`Ctrl + Shift + B`)

The Bookmarks overlay allows you to manage saved links with full keyboard navigation:

- Press **Ctrl + Shift + B** to toggle the Bookmarks Manager.
- **Add Bookmark**: Press **Ctrl + D** while viewing any page to save the current URL into your bookmarks collection.
- **Fuzzy Search**: Start typing inside the overlay to filter bookmarks dynamically.
- **Open Bookmark**: Use the arrow keys to highlight a bookmark and press **Enter** to open it.
- **Delete Bookmark**: Highlight a bookmark and press **Delete** to remove it from your collection.
- Bookmarks are stored in:
  ```text
  ~/.config/fly-browser-native/bookmarks.json
  ```

---

## Actionable History Overlay (`Ctrl + Shift + H`)

![Fly Browser Actionable History Overlay](/screenshots/history.webp)

- Press **Ctrl + Shift + H** to open the History overlay.
- Search through your browsing history in real time.
- Press **Enter** on any result to open the page.
- Click **Purge History** to wipe all stored history records from `history.json`.
