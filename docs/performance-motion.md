# Interface motion

Fly's interface uses two durations and one curve, defined in `style.css`:
`--motion-fast` (120 ms) for feedback such as hover, focus and colour changes,
`--motion-normal` (220 ms) for surfaces such as the sidebar, and
`--motion-ease` (`cubic-bezier(0.2, 0, 0, 1)`) for both. Anything that moves
animates `transform` or `opacity`; colours may fade. Transitions list the
properties they animate instead of `all`. The isolated reader and the What's
New page use the same feedback timing.

## Sidebar

The collapsed rail is the left column of the expanded sidebar, not a second
layout. The workspace badge, favicons and pin sit on the rail's centre line in
both states, every tab row is 40 px tall in both states, and tab text starts
past the rail's edge. Expanding therefore moves only the sidebar's right edge:
no icon, row or line of text changes position or size, and nothing is laid out
again while the edge moves.

The edge moves on the compositor. `#Sidebar` slides from `translateX(-224px)`
to its place while `.sidebar-content` slides back by the same amount, and
`.sidebar-viewport` clips the content to the panel. Both are permanent layers
(`will-change: transform`), so no layer is created or painted again when an
animation starts. Decorations that differ between states crossfade with
`opacity`: the 28 px tile around a selected, pinned or split tab and the frame
around the workspace badge become the full-row highlight and header frame; the
favicon indicators for sleeping, pinned and playing tabs become the row's
badges. Controls that only exist expanded (navigation, clock, update notice,
footer buttons, window controls) fade with the edge. While collapsed they
ignore the pointer but stay focusable, so focusing one expands the sidebar.
The panel's shadow is a separate layer that fades instead of a `box-shadow`
that would be repainted every frame.

The expanded sidebar floats over the page, which never resizes during a hover.
Pinning or unpinning resizes the page once. With the default auto-collapse
setting, Fly starts with the sidebar collapsed instead of animating it closed.
A page's own pointer-entry message is compared with the time of the last
pointer entry into the sidebar, so a message delayed behind the sidebar's own
event cannot close it while the pointer is over it.

`prefers-reduced-motion: reduce` turns off transitions and animations in the
interface, the reader and What's New, and dashboard navigation scrolls
immediately. Websites keep their own motion.

## Other changes

- Download progress fills animate `scaleX` instead of `width`.
- Reader text-size and column-width changes apply at once instead of reflowing
  the article on every frame.
- The update notice stays highlighted without an endless pulse that repainted
  its shadow every frame.
- Busy spinners still turn while an operation runs. Nothing else animates at
  rest.

## Measurements

[Raw summary](/benchmarks/2026-10-15-sidebar-motion.json). Electron 44.7.2 on
the X11/i3 machine used by the other performance pages (Ryzen 7 5800X,
GeForce RTX 3060, 144 Hz display), 60 tabs, the sidebar opened and closed with
the real X11 pointer, recorded with Chromium tracing. "Before" is `main` at
`72a162b`.

Totals over 12 transitions, before → after:

| Configuration | Frames missing an update | Main-thread frame work, ms | Layout, ms | Paint, ms | Raster, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Default GPU | 48.6 % → 1.3 % | 1,386 → 103 | 410 → 1.7 | 381 → 8.3 | 134 → 10.7 |
| Software compositing (`disable_gpu`) | 49.1 % → 0.8 % | 1,414 → 107 | 418 → 2.0 | 384 → 8.4 | 445 → 41.5 |
| Default GPU, interface CPU throttled ×4 | 60.0 % → 3.9 % | 3,331 → 318 | 876 → 7.7 | 758 → 26.9 | 76 → 10.1 |

Frames "missing an update" are frames the compositor presented while the
sidebar was animating but without the animation's latest step, so the motion
repeated a position. Before, the sidebar's width, padding and rows were laid
out again on the main thread of Fly's interface for every step, and that thread
delivered an update for only about every other frame at 144 Hz. Now the
compositor moves both layers on every frame, and the main thread only works
when the state changes.

With the default GPU, 11 of the 12 transitions advanced on every frame,
6.9 ms apart. The first opening after the 60 tabs loaded missed four frames
in its first 35 ms. With the interface's CPU throttled, each transition can
start a frame late while the main thread applies the new state; the slide
itself then runs on the compositor.

These are samples from one machine. They show where the work happens, not a
frame rate or battery life for other hardware. Battery drain and older GPUs and
drivers were not measured.

## Checking it

`node tests/motion-ui.test.js` (part of `npm run test:keyboard-ui`) checks that
the rail and the expanded sidebar share their layout, that only `transform` and
`opacity` animate while the content stays still, that only the rail receives
pointer input, that interrupted transitions settle, that a stale page entry
cannot close the sidebar, that reduced motion applies at once and that nothing
animates at rest.
