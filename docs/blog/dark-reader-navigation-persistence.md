# The Dark Reader Dilemma: Taming Navigation Amnesia, Picture Inversion, and Blend Mode Blackouts

How transient Chromium frames wiped injected stylesheets on redirect, why responsive picture tags and blend modes turned product photos into black voids, and how lifecycle-aware state restored seamless dark mode.

---

## 1. The Daily Friction

Late-night browsing in a dimly lit room is where a web browser reveals its true character. On a Linux setup tailored for focus with dark terminal windows and a muted Tokyo Night desktop theme, opening the wider web can often feel like opening a blast furnace.

Giant retail platforms and reference portals like Amazon UK still offer no native dark mode whatsoever. When you need to research a product or make a purchase at midnight, you are forced to make a miserable choice: squint through blinding white screens or close your laptop.

---

## 2. The Mise Philosophy: Why Built-In, Why Not Extensions?

In mainstream browsers like Chrome or Firefox, the standard advice is predictable: *"just install a third-party extension like Dark Reader."*

However, on low-resource machines and fanless laptops, extensions extract a steep hidden tax. A typical dark mode extension runs continuous background service workers, attaches heavyweight DOM mutation observers that introduce input latency, and devours 300MB to 500MB of extra memory.

When I started building Mise Browser as my daily driver, I established a core architectural principle: essential ergonomic tools should never depend on heavy external add-ons. Features like mouse-free Vim navigation, true tab hibernation, multi-workspace container isolation, and reading comfort belong directly in the browser engine. They should be native, zero-dependency, and virtually free of memory overhead.

To achieve this, I built a dedicated Dark Reader engine into Mise Browser. Pressing `Ctrl + D` or `d` triggers an instant, hardware-accelerated CSS inversion filter that transforms light-mode pages into a crisp, contrast-balanced dark theme.

On static landing pages, the engine felt instantaneous.

Then I tried doing actual shopping on Amazon UK.

---

## 3. The Engineering Challenge: When Reality Hit

As soon as I searched for an item, clicked a product link, or followed a server redirect, the screen violently flashed back to blinding white. Dark Reader was completely lost:
* Every client navigation or server-side redirect dropped straight back into light mode.
* The status bar toggle button desynchronised, reporting Dark Reader as inactive.
* When links opened product listings in new tabs, the child tabs opened in stark white light.

Even worse, when I attempted to force Dark Reader to persist across pages, another bizarre visual defect appeared: on many product listings, the pictures simply vanished. Photos of laptops, books, and monitors either turned into corrupted photographic negatives or collapsed into solid pitch-black rectangles.

I was caught between blinding white redirects and invisible product photos.

---

## 4. The First Attempt (The Minimalist Promise)

When I first implemented `modules/darkReader.js`, I chose a pure CSS-first architecture. Rather than executing heavy AST stylesheet parsing like third-party extensions, Mise Browser injects a clean CSS filter via Electron's guest `<webview>` API:

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

The mathematical theory behind this approach was straightforward:
1. Inverting the root `<html>` element turns light backgrounds dark and dark text light.
2. The `hue-rotate(180deg)` rotation restores original colour hues (preventing blues from becoming sickly oranges).
3. Re-inverting media elements (`img`, `video`, `canvas`) with `invert(100%) hue-rotate(180deg)` cancels the root inversion for photos and illustrations, ensuring they retain their authentic colours.

To apply this stylesheet, I used Electron's native insertion API:

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

It seemed fast, lightweight, and elegant.

---

## 5. The Bugs: Why the Naive Attempt Failed

Testing this implementation across real-world e-commerce and media websites revealed two compounding failures: one in Electron's frame lifecycle, and one in modern CSS rendering specifications.

### Bug 1: The Ephemeral Frame and Navigation Amnesia

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

### Bug 2: The `<picture>` Double Inversion Trap

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

### Bug 3: The `mix-blend-mode: multiply` Blackout

The second visual culprit was even more insidious. On many modern shopping platforms, product images are photographed on pure white backgrounds (`#ffffff`). To make these images seamlessly blend into whatever background colour the website uses without requiring transparent PNGs, developers apply:

```css
.product-image {
    mix-blend-mode: multiply;
}
```

The mathematical formula for CSS `multiply` blending is:

$$\text{Result} = \frac{\text{Foreground Colour} \times \text{Background Colour}}{255}$$

In standard light mode:
* Multiplying against a white background ($\text{RGB } 255$) produces an identity: $\frac{C \times 255}{255} = C$. The white border disappears cleanly.

In Dark Reader mode:
* The root filter has inverted the page background to a deep dark tone (roughly $\text{RGB } 20, 20, 20$).
* Multiplying the product photo against this dark background yields: $\frac{C \times 20}{255} \approx 0$.

The entire image mathematically multiplies into blackness. The image was present in the DOM, properly loaded, and correctly positioned, but rendered as an invisible, solid black void.

---

## 6. The Architectural Fix

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

## 7. The Daily Driver Difference

Solving this architectural puzzle transformed late-night browsing in Mise Browser into a truly frictionless experience.

Today, when I browse high-contrast sites like Amazon UK at midnight:
* Pressing `d` or `Ctrl + D` toggles a high-performance dark theme instantly.
* Navigating through search filters, following checkout redirects, and opening items in split view preserves dark mode without a single blinding white flash.
* Product photographs, charts, and video embeds remain crisp and naturally coloured, free of blend mode blackouts.
* When tabs hibernate in the background to conserve memory, their dark mode preference is preserved when they awaken.
* Most importantly, this capability runs entirely natively in the browser core. It uses zero third-party extensions, introduces zero input lag, and adds zero background RAM overhead.

---

## 8. Take Mise for a Spin

If you spend long hours in front of a screen on Linux and value a keyboard-first browser that respects both your system resources and your eyesight, I would love for you to try Mise Browser as your daily driver.

Mise is fully open source under the GPL-3.0 licence and built specifically for efficient, distraction-free productivity on modern Linux environments (including Wayland, Hyprland, KDE Plasma, and classic desktop setups):

* Check out the [Getting Started Guide](../guide/getting-started.md) to explore its mouse-free workflow and multi-workspace architecture.
* Grab the standalone AppImage or build it from source on [GitHub](https://github.com/Rakosn1cek/Mise-browser).
* Join the discussion, share feedback, or request features on [Reddit (r/mise_browser)](https://www.reddit.com/r/mise_browser/) or [Mastodon](https://mastodon.social/@misebrowser).

Your feedback directly shapes the development of Mise Browser. Take it for a spin, test it with your daily workflow, and let me know how it feels!
