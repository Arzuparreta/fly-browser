# Fly visual identity

This folder holds Fly's visual direction: the decisions, the system, the
masters and the generators that produce them. Open `direction-board.html` in a
browser for the full presentation: the system, both themes mocked up, the icon
and the engraving.

## The idea

**Chassis, paper and ink. One red.** Fly flies on old machines, so it speaks their language: the matte black of a ThinkPad, dot matrix display type, one bit dithering. The fly is treated as a laboratory specimen, drawn in ink and annotated like a data sheet. The only colour is red, which is both the TrackPoint and the fly's compound eyes.

## Decisions agreed with the owner

| Topic | Decision |
| :--- | :--- |
| Default theme | Chasis (dark). Papel (light) must receive the same care. |
| The fly | Seen from above, turned 13° to the right in the icon, and 29.36° to the right in the sidebar engraving and on new tabs. A three quarter perspective was tried and dropped. |
| Line art | Clean contour whose weight swells on the shadow side. No stipple, no grain, modest detail. |
| Eyes | Compound eye facets as a dot lattice that fades with the light. No glint in any line art version. |
| Desktop icon | Modelled colour fly as a die cut vinyl sticker: crisp edge, thin cut line, shadow that hugs the sticker. Its eyes keep a soft glint with a subtle bloom. |
| Sidebar | The fly engraved into the surface (line art, debossed), replacing the old Mise mark. It is 60% the size of the new-tab fly and gives way to it: hidden while a tab on screen (including the other pane of a split view) shows the new tab page, and after leaving it until the destination paints. |
| New tab | A "memory" of the fly: only its lines, fading towards the tail. The red eyes are its only colour. During its own pending navigation it briefly rubs its front legs, then rests, on a 2.4-second cycle. Quiet gives it room beside entry points to native search and bookmarks. |

## System

### Brand palette

| Name | Hex | Use |
| :--- | :--- | :--- |
| Chasis | `#0E0E0D` | Dark theme ground, matte black. |
| Papel | `#ECE6D8` | Light theme ground and plates. |
| Tinta | `#151310` | Text and drawing on paper. |
| Grafito | `#1B1A18` | Panels and cells on chassis. |
| Rojo punto | `#E0261C` | Selection, eyes, hairlines. Never a fill for large areas. |
| Azul panel | `#3F86F0` | Domains and links. |
| Latón | `#C6A04A` | Warnings: insecure page, downloads. |
| Ceniza | `#8D887D` | Secondary text and key hints. |

### Interface tokens per theme

These are the values used in the mockups. The browser's palette is in `chromium/overlay/fly/ui/fly_theme.{h,cc}`; user colour settings override it.

| Token | Chasis | Papel |
| :--- | :--- | :--- |
| Content background | `#121211` | `#ECE6D8` with paper grain |
| Sidebar and status bar | `#0B0B0A` | `#E3DCCB` with paper grain |
| Panel (active tab, cards) | `#1B1A18` | `#F3EEE3` |
| Cell (dot matrix backing) | `#22211E` | `#D8D0BD` |
| Line (borders, rules) | `#2B2A27` | `#CFC6B2` |
| Text | `#E7E1D3` | `#1A1814` |
| Dim text | `#8A857A` | `#7A7365` |
| Accent | `#E0261C` | `#E0261C` |
| Link | `#6AA2F5` | `#2457B8` |

### Type

| Role | Family | Where |
| :--- | :--- | :--- |
| Interface | IBM Plex Sans 400, 500, 600 | Titles, settings, body text. |
| Data | IBM Plex Mono 400, 500, italic 400 | Domains, memory, key hints, captions. |
| Display | Doto 500 to 900 | Clock, mode badge, workspace number, counters. |

All three are under the SIL Open Font License. The browser bundles them (`chromium/overlay/fly/resources/fonts` for the interface, subset `woff2` files in `resources/new_tab/fonts` for its pages) and never fetches fonts at runtime. Doto sits on cells (`Cell` token) the way a departures board does.

### Textures and devices

| Device | Use | Reference |
| :--- | :--- | :--- |
| Dot grid, 16 px pitch | New tab background. | 04 |
| Paper grain | Light theme surfaces only, as a static tiled image. | 07 |
| One bit dither | Empty states and loading placeholders. | 06 |
| Corner brackets | Whatever has keyboard focus (search field, focused panel). | 05 |
| Red hairline, 1 px, right angles | Connects an element to its data. | 08 |
| Red bar, 2 px | Left edge of the active tab or selected row. | 07, 08 |

## Components shown in the mockups

- **Sidebar header:** the text FLY.
- **Workspace card:** workspace number in Doto cells, name, its key hint.
- **Tab board:** title over domain (domain in Link colour, mono). Audio/mute, pin and split-pane controls convey actionable state. The active row gets the panel background and the red bar.
- **Engraving:** the engraved fly at the bottom of the sidebar, under the tabs.
- **New tab:** Quiet keeps the fly memory, a small native-search entry point and a Bookmarks shortcut. The native menu hosts saved-page navigation and editing; the status bar hosts the clock and Diagnostics hosts memory data.
- **Status bar:** mode badge in Doto on a cell (`NORMAL`, `INSERT`, `HINTS`, `PASSTHROUGH`), key hints, and on the right connection security and time.

## Assets in this folder

| File | What it is |
| :--- | :--- |
| `assets/fly-line.svg` | Line art master. Colours are CSS custom properties (`--ink`, `--paper`, `--eye`, `--facet`, `--facet-ink`, `--wash`) so one file serves both themes. |
| `assets/fly-icon.svg` | Colour icon with the sticker die cut. |
| `assets/fly-engraving-{chasis,papel}.svg` | Debossed line art per theme, transparent background. |
| `assets/fly-memory-{chasis,papel}.svg` | New tab memory per theme, transparent background. |
| `assets/png/icon-*.png` | Application icon renders, from 16 to 1024 pixels, plus the monochrome macOS status icon. `scripts/chromium/application-icons.py` synchronizes the native PNG, SVG, ICO and ICNS resources. |
| `assets/png/engraving-{chasis,papel}@2x.png` | Engraving at 336 px for the sidebar's 168 px slot. |
| `assets/png/memory-{chasis,papel}@2x.png` | Three 560 px square poses in a 1680 × 560 strip: rest and two front-leg positions, for a 280 px slot at 2× density. |
| `assets/png/pixel-{16,24,32}.{png,svg}` | Hand-drawn pixel icons for sizes where the illustration turns to mush. |
| `assets/png/paper-grain.png` | Static paper grain tiled over light-theme surfaces. |
| `assets/png/wordmark.png` | FLY set in Doto. |
| `social.html` | 1280 × 640 source for `docs/public/og-image.png`; render it with Chromium after its fonts have loaded. |

### Performance rules

Fly targets old machines without a dedicated GPU. Use the PNG renders in the
interface; do not run the SVG filters (deboss, sticker, blur) at runtime.

New-tab loading uses discrete crops of one decoded PNG, with four 150 ms
movements and 1800 ms at rest. The front-leg poses differ by 24 degrees so the
rub remains readable at the actual display size. A small transparent native
view paints the artwork from the start: Chromium can retain the outgoing
new-tab surface after destroying its document, so renderer timers cannot keep
that surface moving. The artwork belongs to the tab until the destination's
first content paint, then disappears even if that page is still loading.

PNG strips are decoded once off the UI thread and shared between tabs. A single
native one-shot timer runs only during a visible outgoing navigation: five
pose boundaries per 2.4-second cycle, with no frame loop, layout work, runtime
filters or idle timer. Elapsed time determines the pose and next boundary, so
busy machines skip missed boundaries without accumulating timing drift.
Movement starts after 160 ms, stops when hidden or cancelled, and stays still
with reduced motion. The new-tab rotation is computed as `atan2(9, 16)`; only
its displayed angle is rounded.

## Regenerating the assets

```bash
node design/identity/generator/export.js
python3 design/identity/generator/pixels.py
```

To export only the new-tab PNG strips, use `export.js --new-tab-only`; for only
the sidebar engraving, `export.js --sidebar-only`.
`FLY_ART_CHROMIUM` optionally selects the Chromium executable for rasterisation.

`export.js` writes the SVG masters and their PNG renders. SVG export needs only Node; PNG export also needs Playwright with Chromium (`npm i -g playwright`, then run with `NODE_PATH` pointing at the global modules if needed). `generator/anatomy.js` holds the fly's anatomy, `generator/render.js` the line and colour renderers, and the theme values for the engraving and memory sit at the top of `generator/export.js`. `pixels.py` (Pillow) draws the pixel icons and the paper grain.

The browser ships copies of the files it uses in `chromium/overlay/fly/resources/` (`identity/`, `icons/` and `new_tab/identity/`). After regenerating, copy the changed files there.
