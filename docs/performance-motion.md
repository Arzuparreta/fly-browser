# Interface motion

Fly uses short feedback transitions (120 ms) and surface transitions (180 ms),
with `cubic-bezier(0.2, 0, 0, 1)`. The host interface defines these as
`--motion-fast`, `--motion-normal` and `--motion-ease` in `style.css`.
The isolated reader and What's New page use the same feedback timing.

The auto-collapse sidebar keeps a fixed surface and animates its clip. Its
visible rail remains 36 px wide and its expanded surface remains 260 px wide.
The clipped area does not receive pointer input. Compact and expanded control
layouts change once at the state boundary; widths, padding, margins and tab
rows no longer interpolate independently. Pinning changes the page viewport
once instead of repeatedly resizing an embedded website. Long tab lists scroll
without shrinking their rows or navigation controls.

Download fills animate `scaleX` instead of width. Controls explicitly list the
properties they animate. Reader font and column-width changes reflow the
article once. The update indicator stays highlighted without an endless shadow
pulse. Busy indicators still communicate ongoing operations. There are no new
runtime timers, animation loops, duplicated sidebar trees or permanent
`will-change` layers.

OS reduced-motion preferences disable host, reader and What's New transitions
and animations, and dashboard navigation uses immediate scrolling. These rules
apply to Fly's own interface, not arbitrary website content.

## Reproducing the comparison

Run `node tests/motion-ui.test.js` with Playwright available. If it is installed
outside the project, set `FLY_PLAYWRIGHT_MODULE` to its module path. This is an
optional local Electron check, not an additional CI gate. It creates and removes
a disposable profile.

To compare CSS revisions, save the earlier `style.css` to a temporary file and
pass its absolute path as `FLY_MOTION_BASELINE`. The same running application
then exercises each stylesheet with 60 tabs, 12 alternating sidebar states,
and CPU throttling rates of 1 and 6. It reports CDP layout/style durations,
layout counts and sampled requestAnimationFrame intervals. By default it uses
software rendering; `FLY_MOTION_GPU=1` allows the normal GPU path and prints the
actual feature status. `FLY_MOTION_SCREENSHOT` optionally saves the expanded
state for visual inspection.

In the local X11 software-rendered comparison on 2026-10-15:

| CPU throttle | Layouts before → after | Layout time before → after | Frame interval p95 before → after |
| --- | --- | --- | --- |
| 1 | 373 → 18 | 447 → 21 ms | 13.9 → 7 ms |
| 6 | 189 → 18 | 799 → 133 ms | 34.8 → 7 ms |

With GPU compositing enabled, the same comparison at CPU ×6 measured
211 → 18 layouts and 805 → 121 ms of layout time (frame interval p95
34.7 → 7 ms).

These are diagnostic samples on this machine, not portable FPS or battery-life
guarantees. The comparison isolates stylesheet changes, not whole application
revisions. GPU drivers, display refresh, load and hardware affect the results.
Physical old-PC testing and battery discharge measurements remain unverified.

The regression check also covers interrupted transitions, hidden-area hit
testing, focus expansion, long lists, live reduced-motion changes, the absence
of decorative animation at rest, and download fill updates. Companion sidebar,
overlay and keyboard UI tests cover real pointer and keyboard actions.
