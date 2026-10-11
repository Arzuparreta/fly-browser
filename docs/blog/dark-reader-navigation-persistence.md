# The Dark Reader Dilemma: Taming Navigation Amnesia, Picture Inversion, and Blend Mode Blackouts

How transient Chromium frames wiped injected stylesheets on redirect, why responsive picture tags and blend modes turned product photos into black voids, and how lifecycle-aware state restored seamless dark mode.

---

## 1. The Problem

Browsing late at night on low-resource hardware has always been one of my primary use cases for Mise Browser. While the browser interface itself is clad in a soothing Tokyo Night palette (`#1a1b26`), the broader web remains aggressively blinding. Giant retail platforms and reference portals like Amazon UK still offer no native dark mode whatsoever.

To combat eye fatigue without bogging down the JavaScript runtime with heavy third-party extensions, Mise Browser includes a zero-dependency, built-in Dark Reader engine. Pressing `Ctrl + D` or `d` instantly applies a high-performance CSS inversion filter, transforming blinding white pages into a dark, readable theme.

The feature worked wonderfully on static landing pages.

Then I tried doing actual shopping on Amazon.

As soon as I searched for an item, clicked a link, or followed a server redirect, the screen violently flashed back to blinding white. Dark Reader was completely lost:
* Every client navigation or server-side redirect dropped straight back into light mode.
* The status bar toggle button desynchronised, showing Dark Reader as inactive.
* When links opened product listings in new tabs, the child tabs opened in stark white light.

Even worse, when I attempted to force Dark Reader to persist across pages, another bizarre visual defect appeared: on many product listings, the pictures simply vanished. Photos of laptops, books, and monitors either turned into corrupted photographic negatives or collapsed into solid pitch-black rectangles.

I was caught between blinding white redirects and invisible product photos.

---

## 2. The Solution (My First Attempt)

When I first implemented the Dark Reader module (`modules/darkReader.js`), I chose a minimalist, CSS-first architecture. Rather than executing heavy AST parsing or dynamically rebuilding DOM styles like full-featured browser extensions, Mise Browser injects a clean, hardware-accelerated CSS filter via Electron's guest `<webview>` API:

```javascript
// Minimalist Dark Reader stylesheet
export const DARK_READER_CSS = `
html {
    min-height: 100vh !important;
    background-color: #ffffff !important;
    filter: invert(90%) hue-rotate(180deg) contrast(88%) !important;
}

body {
    min-height: 100vh !important;
}

/* Re-invert media elements so photos preserve natural colours */
img, video, canvas, svg:not(:root), picture, embed, object, iframe {
    filter: invert(100%) hue-rotate(180deg) !important;
}

[style*="background-image"]:empty {
    filter: invert(100%) hue-rotate(180deg) !important;
}

img {
    background-color: transparent !important;
}
`;
```

The mathematical theory behind this approach is elegant:
1. Inverting the root `<html>` element turns light backgrounds dark and dark text light.
2. The `hue-rotate(180deg)` rotation restores original color hues (preventing blues from becoming sickly oranges).
3. Re-inverting media elements (`img`, `video`, `canvas`) with `invert(100%) hue-rotate(180deg)` cancels the root inversion for photos and illustrations, ensuring they retain their authentic colours.

To apply this stylesheet, I relied on Electron's native insertion API:

```javascript
// My initial naive toggle implementation
export async function setWebviewDarkReader(wv, enable) {
    if (!wv || typeof wv.insertCSS !== 'function') return false;
    if (enable) {
        if (!wv.__miseDarkKey) {
            const key = await wv.insertCSS(DARK_READER_CSS);
            wv.__miseDarkKey = key;
        }
        return true;
    } else {
        if (wv.__miseDarkKey) {
            await wv.removeInsertedCSS(wv.__miseDarkKey);
            wv.__miseDarkKey = null;
        }
        return false;
    }
}
```

If the key existed (`wv.__miseDarkKey`), the tab was considered dark. If the key was null, the tab was considered light.

It seemed straightforward, fast, and completely self-contained.

---

## 3. The Bug / The Fail

Testing this implementation across real-world e-commerce and media websites revealed two compounding failures: one in Electron's frame lifecycle, and one in modern CSS rendering specifications.

### Failure 1: The Ephemeral Frame and Navigation Amnesia

The first failure stemmed from how Chromium treats inserted stylesheets.

When calling `webview.insertCSS()`, Chromium attaches the stylesheet to the active document frame. The moment any navigation or redirect occurs (`did-navigate`):
1. Chromium unloads the existing document frame.
2. All inserted stylesheets associated with that frame are immediately purged from memory.
3. The guest webview begins loading the destination URL with a completely blank slate.

Because my code relied entirely on `wv.__miseDarkKey` as the source of truth, two things broke simultaneously:
* The webview was now rendering pure light-mode HTML, but `wv.__miseDarkKey` still held a stale, dangling string from the previous frame.
* The browser assumed Dark Reader was already active, yet the newly rendered DOM had zero Dark Reader styles applied.

Furthermore, on modern websites like Amazon, clicking search results or product tiles often opens links with `target="_blank"`. In Electron, this emits a `new-window` event. My event handler created a brand new `<webview>` instance for the tab, but this new instance had no awareness that its parent tab had Dark Reader enabled.

Finally, single-page application (SPA) navigations (which use `history.pushState` and emit `did-navigate-in-page`) bypassed standard reload hooks entirely, leaving partial DOM updates unstyled.

### Failure 2: The `<picture>` Double Inversion Trap

When I attempted to automatically re-inject `DARK_READER_CSS` upon every page load, the second, more puzzling bug surfaced: on multiple pages, pictures were completely distorted, turned into dark photographic negatives, or failed to display altogether.

I inspected a broken product listing on Amazon and discovered the first culprit. The markup was not a simple `<img>` tag:

```html
<picture class="s-image-picture">
    <source type="image/avif" srcset="...">
    <source type="image/webp" srcset="...">
    <img src="product.jpg" class="s-image" alt="Product">
</picture>
```

Look closely at the media selector I had written:

```css
img, video, canvas, svg:not(:root), picture, embed, object, iframe {
    filter: invert(100%) hue-rotate(180deg) !important;
}
```

In the HTML5 specification, `<picture>` is not a replaced media element. It is an inline wrapper container that provides responsive sources for its child `<img>`.

Because both `picture` and `img` matched the selector, Chromium applied the filter to the container **and** to the child image:
1. Root `<html>` was inverted (1st inversion: normal image becomes negative).
2. The `<picture>` container was re-inverted (2nd inversion: negative becomes normal).
3. The child `<img>` inside the container was re-inverted **again** (3rd inversion: normal becomes negative!).

Every single responsive `<picture>` element on the web underwent a triple inversion. White product backdrops inverted into pitch-black, and natural product colours turned into muddy negatives.

### Failure 3: The `mix-blend-mode: multiply` Blackout

The second visual culprit was even more insidious. On many modern shopping platforms, product images are photographed on pure white backgrounds (`#ffffff`). To make these images seamlessly blend into whatever background color the website uses without requiring transparent PNGs, developers apply:

```css
.product-image {
    mix-blend-mode: multiply;
}
```

The mathematical formula for CSS `multiply` blending is:

$$\text{Result} = \frac{\text{Foreground Color} \times \text{Background Color}}{255}$$

In standard light mode:
* Multiplying against a white background ($\text{RGB } 255$) produces an identity: $\frac{C \times 255}{255} = C$. The white border disappears cleanly.

In Dark Reader mode:
* The root filter has inverted the page background to a deep dark tone (roughly $\text{RGB } 20, 20, 20$).
* Multiplying the product photo against this dark background yields: $\frac{C \times 20}{255} \approx 0$.

The entire image mathematically multiplies into blackness. The image was present in the DOM, properly loaded, and correctly positioned, but rendered as an invisible, solid black void.

---

## 4. The Architectural Fix

To solve these compounding failures, I separated the architectural state from the ephemeral Chromium frame, and completely overhauled the CSS media inversion rules.

### Step 1: Leaf-Only Media Inversion and Blend Mode Normalisation

I updated `modules/darkReader.js` to ensure container wrappers are never inverted, and forced media blend modes to `normal`:

```javascript
/* Re-invert leaf media elements so photos, videos, canvases and charts preserve natural colours */
img, video, canvas:not(.pdf-page-canvas), svg:not(:root), embed, object, iframe {
    filter: invert(100%) hue-rotate(180deg) !important;
    mix-blend-mode: normal !important;
}

/* Reset mix-blend-mode and prevent double inversion on picture containers */
picture {
    filter: none !important;
    mix-blend-mode: normal !important;
}
```

By explicitly setting `picture { filter: none !important; }`, the `<picture>` wrapper remains neutral. The child `<img>` receives exactly one re-inversion, flawlessly preserving natural colours.

Simultaneously, `mix-blend-mode: normal !important;` prevents e-commerce product photos from multiplying into dark backgrounds, completely eliminating the blackout bug.

### Step 2: Tab-Level State Tracking in the Central Store

Rather than treating the transient frame key as the state flag, I introduced a dedicated `tabDarkReaderStates` map within the central state store (`modules/state.js`).

When Dark Reader is toggled:

```javascript
export async function toggleDarkReaderOnActiveTab() {
    const currentWS = state.sessionState.current_workspace;
    const activeWv = getActiveWebview();
    if (!activeWv) return;

    const willEnable = !isWebviewDarkReaderActive(activeWv);
    await setWebviewDarkReader(activeWv, willEnable);

    // Track state in central store
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
    if (currentIdx >= 0) {
        if (!state.tabDarkReaderStates) state.tabDarkReaderStates = {};
        if (!state.tabDarkReaderStates[currentWS]) state.tabDarkReaderStates[currentWS] = [];
        state.tabDarkReaderStates[currentWS][currentIdx] = willEnable;
    }
    ...
}
```

### Step 3: Lifecycle-Aware Navigation Reapplication

In `modules/webview.js`, I wired Dark Reader into every stage of the webview lifecycle:

1. **On `did-navigate`**: When a new page commits, reset the stale key (`webview.__miseDarkKey = null`).
2. **On `dom-ready`**: If `webview.__miseDarkReaderEnabled` is true, reapply the stylesheet immediately:
   ```javascript
   if (webview.__miseDarkReaderEnabled) {
       reapplyWebviewDarkReader(webview).catch(() => {});
   }
   ```
3. **On `did-navigate-in-page`**: Reapply the stylesheet across SPA client route transitions.
4. **On `did-finish-load`**: Provide a safety fallback check if DOM ready occurred before a fast redirect.
5. **On `new-window`**: Propagate the parent tab's Dark Reader state to child tabs:
   ```javascript
   const inheritsDarkReader = !!webview.__miseDarkReaderEnabled;
   ...
   if (inheritsDarkReader) {
       if (!state.tabDarkReaderStates) state.tabDarkReaderStates = {};
       if (!state.tabDarkReaderStates[currentWS]) state.tabDarkReaderStates[currentWS] = [];
       state.tabDarkReaderStates[currentWS][newTargetIdx] = true;
   }
   ```
6. **On Tab Hibernation and Wake**: Persist `darkReader: isDarkReader` inside `state.tabSleepStates` so sleeping tabs awaken with Dark Reader preserved.

---

## 5. Key Takeaways

1. **Never conflate injection tokens with application state**: In Chromium, `<webview>.insertCSS()` tokens belong to the active frame, not the webview instance. Storing persistent state on ephemeral native handles guarantees desynchronisation on the first redirect.
2. **`<picture>` is a wrapper, not an image**: Inverting container elements alongside their children causes compound filter operations. Leaf media elements (`<img>`, `<video>`, `<canvas>`) should be re-inverted, while parent containers must remain neutral.
3. **Beware of CSS blend mode math in dark themes**: Properties like `mix-blend-mode: multiply` rely on white backgrounds to achieve transparency. Inverted dark backgrounds break that mathematical assumption and turn images completely black. Always normalise blend modes when applying forced colour inversions.
4. **Child tabs inherit reading context**: If a user toggles Dark Reader on a domain, opening links in new tabs should honour that choice seamlessly rather than jarring the user with unstyled white canvases.
