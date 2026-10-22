# Tab Suspension

Fly pauses eligible background pages using Chromium's frozen lifecycle state. The page stays in memory: selecting its tab resumes the same document without reloading it. Form values, JavaScript state, navigation history and scroll position stay with that document.

Suspension saves CPU and battery. It does **not** promise the memory savings of unloading a page. Fly never automatically destroys an open page to save memory, including when memory is low. Browser or renderer crashes can still lose unsaved work; suspension is not a backup.

## Automatic suspension

In **Preferences → Performance → Suspend background tabs**, choose **Off** or 5, 15, 30 or 60 minutes. The default is 15 minutes. The native **Fly Settings → Tab Suspension** menu offers the same choices.

Time counts continuously from when the page becomes hidden, including switching to another workspace. Fly checks every 30 seconds. Selecting a page restarts its background interval the next time you leave it.

Fly protects visible pages, including both split panes, and pinned tabs. It also leaves pages running when they are loading, playing media, capturing, downloading, or have active requests, calls or persistent connections. Pages with DevTools open are excluded. If eligibility cannot be established, or Chromium rejects suspension, the page remains alive with normal background throttling.

Open WebSocket and EventSource connections, live media tracks, unclosed audio contexts and peer connections prevent suspension. Pages holding Web Locks, retained IndexedDB transactions or worker objects are also excluded, as are pages with frames whose state Fly cannot inspect. These conservative checks favour keeping applications working over suspending every page. Busy pages are reconsidered on a later scan.

Suspension can pause application timers and callbacks. It preserves the local document; it cannot guarantee that a remote service will never expire a session or change its data while you are away.

## Manual controls

Open the Command Palette and choose:

- **Suspend Inactive Tabs**: pause eligible background pages across workspaces, using the same protections as automatic suspension.
- **Resume Tabs in Workspace**: resume existing pages in the current workspace without reloading them. Tabs not yet opened since startup remain unloaded.

Choosing **Never** resumes suspended pages immediately. There are no sleep moons or IN FLIGHT, LANDED or PLAYING text badges. The audio and mute controls remain available.

## Starting and recovering a session

Restoring a session after the browser has closed is separate from suspension. Background tabs load on demand so that starting Fly does not load every saved website at once. Opening such a tab requires loading the website; there is no live document from the previous process to resume.

Fly still uses atomic session writes, a backup session file and an exit sentinel for recovery after unexpected shutdowns. These protect the saved session, not arbitrary unsaved website content.

Existing hibernation settings migrate to suspension with the chosen timeout preserved, including **Never**. Saved URLs, titles and any previously captured scroll or session-storage data remain readable. New runtime suspension neither captures nor reconstructs a page's state.
