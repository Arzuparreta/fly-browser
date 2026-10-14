# Fly visual identity: design handoff

This folder hands the agreed visual direction to whoever implements it in the application. It holds the decisions, the system, the exported assets and the generator that produced them. Nothing here is wired into the app yet.

Open `direction-board.html` in a browser for the full presentation: reading of the references, system, both themes mocked up, icon and engraving. The ten reference images it discusses are `style_reference_1.jpg` to `style_reference_10.jpg` in the repository root.

## The idea

**Chassis, paper and ink. One red.** Fly flies on old machines, so it speaks their language: the matte black of a ThinkPad, dot matrix display type, one bit dithering. The fly is treated as a laboratory specimen, drawn in ink and annotated like a data sheet. The only colour is red, which is both the TrackPoint and the fly's compound eyes.

## Decisions agreed with the owner

| Topic | Decision |
| :--- | :--- |
| Default theme | Chasis (dark). Papel (light) must receive the same care. |
| The fly | Seen from above, turned 13° to the right. A three quarter perspective was tried and dropped. |
| Line art | Clean contour whose weight swells on the shadow side. No stipple, no grain, modest detail. |
| Eyes | Compound eye facets as a dot lattice that fades with the light. No glint in any line art version. |
| Desktop icon | Modelled colour fly as a die cut vinyl sticker: crisp edge, thin cut line, shadow that hugs the sticker. Its eyes keep a soft glint with a subtle bloom. |
| Sidebar | The fly engraved into the surface (line art, debossed), replacing the old Mise mark. |
| New tab | A "memory" of the fly: only its lines, fading towards the tail, letting the dot grid show through. The red eyes are its only colour. Red hairlines annotate it with live browser data. |

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

These are the values used in the mockups. Map them onto the existing theme system (`modules/theme.js`, the `:root` and `body.light-mode` variables in `style.css` and the user colour settings) rather than hard coding them.

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

All three are under the SIL Open Font License. Bundle them inside the app as subset `woff2` files; the app must not fetch fonts at runtime. Doto sits on cells (`Cell` token) the way a departures board does.

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

- **Sidebar header:** small colour fly, `FLY` in Doto, window controls.
- **Workspace card:** workspace number in Doto cells, name, container, its key hint.
- **Tab board:** columns for jump number, title over domain (domain in Link colour, mono), state and memory. States in the mockups were written in Spanish for review. Interface copy must be UK English, for example `IN FLIGHT` (active), `LANDED` (hibernated) and `PLAYING` (audio). Landed tabs use dim text. The active row gets the panel background and the red bar.
- **Engraving:** the engraved fly at the bottom of the sidebar, under the tabs, replacing `#Sidebar::before` and `assets/logo.svg`.
- **New tab:** Doto clock on cells, date in spaced mono capitals, search field with corner brackets and its `/` hint, a short list of frequent sites with key hints, and the fly memory with hairline annotations.
- **Status bar:** mode badge in Doto on a cell (`NORMAL`, `INSERT`, `HINTS`, `PASSTHROUGH`, `SPLIT` already exist in `modules/statusBar.js`), workspace, key hints, and on the right the blocker state, connection security and time.

### Data behind the annotations

Annotate only with values the browser really has. Checked in the code on 14 October 2026:

| Annotation | Source |
| :--- | :--- |
| Memory in use | `app.getAppMetrics()` is already read in `main.js` and `diagnostics.js`. |
| Landed (hibernated) tabs | Hibernation state lives in `modules/webview.js` (`hibernateTab`, `hibernatedAt`). |
| Telemetry | Always zero by policy; a fixed statement, not a measurement. |
| Trackers blocked | **No counter exists.** Add one from the Ghostery blocker events only if it costs nothing measurable; otherwise drop this annotation. |

## Assets in this folder

| File | What it is |
| :--- | :--- |
| `assets/fly-line.svg` | Line art master. Colours are CSS custom properties (`--ink`, `--paper`, `--eye`, `--facet`, `--facet-ink`, `--wash`) so one file serves both themes. |
| `assets/fly-icon.svg` | Colour icon with the sticker die cut. |
| `assets/fly-engraving-{chasis,papel}.svg` | Debossed line art per theme, transparent background. |
| `assets/fly-memory-{chasis,papel}.svg` | New tab memory per theme, transparent background. |
| `assets/png/icon-{1024,512,256,128,64,48}.png` | Icon renders. 1024 feeds `build/icon.png` (electron-builder derives the macOS icon from it), the rest cover Linux hicolor sizes and `build/icon.ico`. |
| `assets/png/engraving-{chasis,papel}@2x.png` | Engraving at 512 px for a 256 px slot. |
| `assets/png/memory-{chasis,papel}@2x.png` | Memory at 1200 px. |

### Performance rules for the implementation

Fly targets old machines without a dedicated GPU.

- Use the PNG renders in the interface. Do not run the SVG filters (deboss, sticker, blur) at runtime.
- The current sidebar and several overlays use `backdrop-filter: blur(16px)`, which is costly without a GPU. The new direction does not need it; removing it is the owner's call.
- `index.html` loads Font Awesome from a CDN. With fonts bundled locally, icons should be local too, for privacy and offline start.

## Still open

1. **Small icons, 16, 24 and 32 px.** The illustration turns to mush below 48 px. These need a hand drawn pixel art fly that keeps the two red eyes. Until then do not ship 16 to 32 px from the vector.
2. **Wordmark.** The mockups set `FLY` in Doto; no separate lockup has been drawn.
3. **Social image and documentation logo** (`docs/public/og-image.png`, `docs/public/logo.png`): not redrawn yet.
4. **Fork fixes ledger.** The implementation PR should record the identity change and close the placeholder icon limit noted under FLY-01 in `docs/fork-fixes.md`.

## Regenerating the assets

```bash
node design/identity/generator/export.js
```

SVG export needs only Node. PNG export also needs Playwright with Chromium (`npm i -g playwright`, then run with `NODE_PATH` pointing at the global modules if needed). `generator/anatomy.js` holds the fly's anatomy, `generator/render.js` the line and colour renderers, and the theme values for the engraving and memory sit at the top of `generator/export.js`.
