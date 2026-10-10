# Engineering Chronicles

Deep dives into the most challenging bugs, feature failures, and architectural lessons encountered during the development of Mise Browser.

Each chronicle follows the development anatomy: **Problem** → **Attempted Solution** → **The Bug / Fail** → **The Architectural Fix**.

---

### [The Blind Spot in the Browser: Conquering Shadow DOM and the 144-Key Limit in Link Hints](./shadow-dom-link-hints.md)

How Web Components hid navigation links from query selectors, why hardcoded 2-letter permutations created silent key collisions, and how recursive shadow tree crawling restored mouse-free navigation.

`DOM` · `Web Components` · `Keyboard Navigation` · `Link Hints` · `Vim`

---

### [The Accidental Resurrection: How Closing a Tab Defeated Tab Hibernation](./accidental-tab-resurrection.md)

How a naive index decrement broke tab closure ergonomics, why sleeping background tabs were silently woken into memory, and how Most Recently Used (MRU) lifecycle awareness restored efficiency.

`Tab Management` · `Memory Optimisation` · `State Architecture` · `Ergonomics` · `Electron`

---

### [The Google Auth Botguard Saga: The rrk=46 Riddle and Poisoned Cookies](./google-auth-rrk46-botguard.md)

How an attempt to sign into Gmail in an isolated container led down a rabbit hole of Botguard detection, User-Agent traps, and self-perpetuating cookie poisoning.

`Security` · `Authentication` · `Chromium` · `Electron`

---

### [The Shadow DOM Keyboard Trap: When Reddit Swallowed My Vim Keys](./shadow-dom-keyboard-trap.md)

How modal keyboard shortcuts broke inside modern Web Components, why `document.activeElement` lied to me, and how deep shadow root penetration restored keyboard control.

`DOM` · `Web Components` · `Keyboard Navigation` · `Vim`

---

### [The "Zero-RAM" Tab Hibernation Myth: Why Sleeping Tabs Still Cost 40-90MB](./tab-hibernation-zero-ram-myth.md)

How chasing zero memory usage broke tabs with the `about:blank` amnesia trap, why Chromium partition sessions retain 40-90MB of RAM, and the pragmatic unmounting architecture that slashed memory bloat safely.

`Memory Optimisation` · `Chromium` · `Electron` · `Linux` · `Architecture`

---

### Upcoming Chronicles

- **The Ghostery IPC Civil War: Adblocking Across Isolated Partitions**  
  How strict multi-account container partitions caused IPC channel collisions in the adblocking engine and the session detachment fix that resolved it.

- **Taming the Userscript Engine: SSRF and DNS Rebinding Defences**  
  Closing critical security holes in `GM_xmlhttpRequest` by enforcing `@connect` rules, private IP blocking, and pre-resolution socket pinning.

- **The Ghost in the CSS: Tracking Pixels in Reader View**  
  How inline CSS background images and SVG containers bypassed DOM distillation and how strict sanitisation closed the leak.

---

### Community & Discussion

Interested in discussing technical architectural decisions, bugs, or upcoming chronicles? Join the conversation on [Reddit (r/mise_browser)](https://www.reddit.com/r/mise_browser/) or connect on [Mastodon (@misebrowser@mastodon.social)](https://mastodon.social/@misebrowser).
