# The Blind Spot in the Browser: Conquering Shadow DOM and the 144-Key Limit in Link Hints

How Web Components hid navigation links from query selectors, why hardcoded 2-letter permutations created silent key collisions, and how recursive shadow tree crawling restored mouse-free navigation.

---

## 1. The Problem

In Mise Browser, link hinting (`f` or `Ctrl + F`) is the centrepiece of mouse-free web navigation. Inspired by Vimium and Tridactyl, pressing `f` overlays small cyan badges containing short letter chords directly over every clickable link, button, and input on the active page. Typing the corresponding letter combination instantly clicks or focuses that element.

When browsing classic websites, link hinting felt instantaneous.

Then I opened modern Reddit to browse a subreddit.

When I pressed `f`, badges scattered across posts in the central feed, but major areas of the interface remained completely bare:
* The left navigation drawer ("Home", "Popular", "News", "Explore") had zero badges.
* The top-left Reddit brand logo had no badge.
* The main search bar had no badge.
* Moderator and community buttons in the right sidebar had no badges.

Entire functional sections of the page were dead zones for keyboard navigation. I had to reach for the touchpad just to return to my home feed.

At the same time, I noticed an unsettling question about the underlying key distribution: on pages with hundreds of items, could the hinter simply be running out of key combinations?

---

## 2. The Solution (My First Attempt)

When I originally wrote `hinter.js`, the implementation followed the textbook browser extension recipe:

1. Query all interactive elements across the document.
2. Filter for visible elements within the viewport.
3. Assign each element a two-letter combination from a dedicated 12-letter left-hand cluster (`q, w, e, a, s, d, z, x, c, r, f, v`), keeping the right hand free and strictly avoiding Vim directional keys (`h, j, k, l`).
4. Mount an absolute overlay layer to render the badges.

The core discovery logic was succinct:

```javascript
// My initial naive element query
const links = Array.from(
    document.querySelectorAll('a, button, input, textarea, select, [role="button"]')
);

const visible = links.filter(l => {
    const rect = l.getBoundingClientRect();
    return rect.width > 0 && rect.height > 0 && rect.top >= 0 && rect.top <= window.innerHeight;
});
```

And to map each item to a two-letter key chord:

```javascript
visible.forEach((el, index) => {
    const first = charset[Math.floor(index / charset.length) % charset.length];
    const second = charset[index % charset.length];
    const code = first + second;
    activeHints[code] = el;
    ...
});
```

For simple pages with forty or fifty links, this produced tidy combinations like `QA`, `QW`, and `ER`.

---

## 3. The Bug / The Fail

Inspecting the Reddit page with developer tools uncovered two compounding architectural failures: one in DOM discovery, and one in key permutation math.

### Failure 1: The Shadow DOM Encapsulation Blindfold

Modern web applications like Reddit ("Shreddit"), YouTube, and GitHub increasingly build their interfaces out of **Web Components**.

When I inspected Reddit's left navigation drawer, I found that the links were not plain `<a>` tags in the document tree. They were encapsulated inside nested custom elements:

```html
<shreddit-sidebar>
  #shadow-root (open)
    <flex-left-nav-container>
      #shadow-root (open)
        <a href="/">Home</a>
        <a href="/r/popular">Popular</a>
```

By design, `document.querySelectorAll()` **only queries the document's top-level light DOM**. It stops dead at shadow boundaries. To standard DOM selectors, everything inside an open `#shadow-root` is completely invisible.

Because the left navigation drawer lived entirely within a shadow tree, `querySelectorAll` never saw a single link. 

Furthermore, my visibility check contained an edge-case flaw:

```javascript
rect.top >= 0 && rect.top <= window.innerHeight
```

If a sticky header or navigation link was scrolled even 1 pixel past the top edge (`rect.top = -1`), it was immediately discarded, even if 95% of the button remained visible in the viewport.

### Failure 2: The 144-Key Modulo Collision Ceiling

The second flaw was mathematical. 

My left-hand cluster contained exactly 12 characters:
```javascript
const charset = ['q', 'w', 'e', 'a', 's', 'd', 'z', 'x', 'c', 'r', 'f', 'v'];
```

A fixed two-letter mapping provides:
$$12 \times 12 = 144 \text{ combinations}$$

What happens when a dense page (like Reddit comments, Wikipedia reference lists, or Hacker News) contains 150 visible links?

Look at the modulo calculation:
```javascript
const first = charset[Math.floor(index / charset.length) % charset.length];
const second = charset[index % charset.length];
```

When `index` reached 144:
* `first` evaluated to `charset[12 % 12]` -> `charset[0]` (`q`)
* `second` evaluated to `charset[144 % 12]` -> `charset[0]` (`q`)
* `code` became `qq`

Link 144 received the exact same badge code as Link 0. `activeHints['qq']` was silently overwritten, and a duplicate badge was painted directly over the interface. If you typed `qq`, the browser could only trigger the second element. The 144-key ceiling was creating silent key collisions and orphaned links.

---

## 4. The Architectural Fix

To resolve both issues while strictly preserving the integrity of Mise's Vim-style navigation, I overhauled `hinter.js` across four layers.

### A. Recursive Shadow Tree Crawling

I replaced `document.querySelectorAll()` with a recursive DOM walker that traverses light DOM nodes and transparently dives into every open `#shadow-root`:

```javascript
function collectInteractiveElements(root) {
    const selector = 'a, button, input, textarea, select, details, summary, [role="button"], [role="link"], [role="tab"], [role="menuitem"], [role="checkbox"], [role="radio"], [role="switch"], [role="option"], [contenteditable="true"], [tabindex]:not([tabindex="-1"]), [onclick]';
    const elements = [];

    function traverse(node) {
        if (!node) return;
        if (node.nodeType === 1) { // Node.ELEMENT_NODE
            if (node.matches && node.matches(selector)) {
                elements.push(node);
            }
            // Penetrate open Shadow DOM boundaries in Web Components
            if (node.shadowRoot) {
                traverse(node.shadowRoot);
            }
        }
        let child = node.firstElementChild;
        while (child) {
            traverse(child);
            child = child.nextElementSibling;
        }
    }

    traverse(root);
    return elements;
}
```

This single change immediately unlocked every link, button, and input nested inside Reddit's sidebar, header, and community cards.

### B. Precise Viewport Bounds & Rect Deduplication

The visibility filter was upgraded to test complete 4-sided viewport bounds and verify that computed visibility, opacity, and display styles are not hidden:

```javascript
// Complete viewport intersection
if (rect.bottom <= 0 || rect.top >= window.innerHeight || rect.right <= 0 || rect.left >= window.innerWidth) {
    return;
}
```

To prevent overlapping badges where custom elements wrap native buttons at the exact same coordinates, I introduced coordinate deduplication to ensure only one badge renders per visual target.

### C. Collision-Free Dynamic Key Permutations

To solve the 144-key limit without adding new keys that might collide with Vim shortcuts, I implemented a dynamic base-N permutation generator:

```javascript
function generateHintCodes(count, alphabet) {
    const base = alphabet.length;
    let len = 1;
    let maxCombos = base;
    while (maxCombos < count) {
        len++;
        maxCombos = Math.pow(base, len);
    }

    const codes = [];
    for (let i = 0; i < count; i++) {
        let code = '';
        let num = i;
        for (let pos = len - 1; pos >= 0; pos--) {
            const digit = Math.floor(num / Math.pow(base, pos)) % base;
            code += alphabet[digit];
        }
        codes.push(code);
    }
    return { codes, codeLength: len };
}
```

This algorithm dynamically scales badge length based on the number of targets:
* $\le 12$ links: **1-character hints** (instant single-key execution).
* $13 - 144$ links: **2-character hints** (clean two-stroke navigation).
* $> 144$ links: **3-character hints** (up to 1,728 unique links with zero collisions).

Crucially, the charset remains strictly confined to the 12-key left-hand cluster:
```javascript
const charset = ['q', 'w', 'e', 'a', 's', 'd', 'z', 'x', 'c', 'r', 'f', 'v'];
```

Vim navigation keys (`h, j, k, l`, `g`, `u`, `i`, `y`) are completely untouched and safe from conflict.

### D. Keystroke Isolation & Visual Typing Feedback

When hints are active, `hintKeyListener` now captures all keystrokes with `e.preventDefault()` and `e.stopPropagation()`. No non-hint key can accidentally leak through to trigger background page events or Normal mode Vim handlers.

Typing also received instant visual feedback:
* As you type the first letter of a multi-character chord, matching badges highlight in Tokyo Night orange (`#ff9e64`) while non-matching badges dim.
* Pressing `Backspace` un-types the last character to correct typos without cancelling the layer.
* Pressing `Escape` cleanly dismisses the overlay.

---

## 5. Architectural Lessons & Takeaways

### Web Components Require Shadow-Aware Tooling
Modern front-end frameworks increasingly embrace encapsulation. If you build browser extensions, scraping tools, or keyboard navigation engines using traditional `document.querySelectorAll()` calls, modern websites will be full of blind spots. Walking open shadow roots is mandatory for full DOM coverage.

### Never Hardcode Combinatorial Ceilings
Assuming a page will have fewer than $N$ interactive targets is a trap. Dynamic permutation scaling ensures that whether a page contains three buttons or eight hundred search results, every link receives a unique, predictable chord.

### Modal Keyboards Require Full Event Trapping
When a modal input state (like link hints) is active, it must claim exclusive ownership of the keyboard stream. Trapping unhandled keys prevents subtle race conditions and protects underlying Vim navigation mappings from unexpected triggers.
