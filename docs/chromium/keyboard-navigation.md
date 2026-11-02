# Native keyboard navigation

Fly keeps one native keyboard router and command catalogue for page keys,
shortcuts, the Command Centre and the recorder. This note records the scroll
and link-hint design and the tradeoffs against bundling Vimium.

## Diagnosis and reference

The previous scroll patch called Blink's animated relative scroll for every
80-pixel command, including OS key repeats. A held key therefore moved one step,
waited for the system repeat delay, then repeatedly redirected smooth-scroll
animations. That is a different input model from Vimium's continuous movement.

The old hint controller gathered its first on-screen accessibility snapshot
and cancelled the overlay if any target's bounds later changed. Blink sends a
newly enabled accessibility tree in batches; a later batch or page layout shift
could cancel the first hints without user input. Reopening then used a warmer,
more complete tree. This was reproduced on GitHub during the investigation.

The reference implementation inspected was
[Vimium at 5aa29614](https://github.com/philc/vimium/tree/5aa29614bf1dce05e0d316f8c38722e17f9b38c3):

- `content_scripts/scroller.js`: frame-paced movement, a minimum step duration
  of 100 ms, held-key calibration after 75 ms, and no new animation on repeat.
- `content_scripts/link_hints.js`: short prefix-free alphabet labels, prefix
  filtering and Backspace.
- Vimium tracks the last activated DOM element and walks its scrollable
  ancestors. Fly uses compositor hit testing at the last click or hint point.

The adapted algorithm attribution and MIT license ship in
`fly_resources/licenses/vimium-MIT.txt`.

Fly's native keyboard and accessibility hints were introduced in merged
commit `485d4bc` (PR #114), following the native keyboard contracts in
`f316184` (PR #111). The repository history does not document an experiment
bundling Vimium that should be treated as a prior compatibility verdict.

## Why keep the native integration

Vimium is a strong interaction reference. Bundling it as-is would add its own
content-script key handlers, modes, mappings, settings, background commands and
frame messaging alongside Fly's existing router. Tab, window and URL commands
would need adapters for Fly's workspace profiles and native surfaces. Either
users would configure two systems or Fly would maintain a fork that delegates
most browser commands back to the native catalogue. Extension content-script
restrictions would also leave internal pages and the browser interface on a
separate input path.

The selected implementation keeps those contracts native and replaces the
scroll mechanism. It has no extension dependency and no page-injected script.
The scroll motion is isolated from Chromium input plumbing, so its timing can
be tested without a browser. Link hints continue to use Chromium accessibility
for clickable targets, coordinate transforms and activation across frames.

## Scroll ownership

- `commands/scroll_motion.*` owns distance, timing and held-key calibration.
  One tap owes 60 CSS pixels, scaled with page zoom. Distinct taps add distance;
  OS auto-repeat never queues extra steps or restarts the animation.
- `ui/page_scroller.*` owns gesture lifetime. The window compositor paces
  precise-pixel synthetic autoscroll gestures to the focused frame's widget.
  Eligible scroll nodes move on the renderer compositor thread. This avoids
  executing a JavaScript scroll function for each frame, but does not promise
  that every page can scroll without main-thread involvement.
- Key release ends the hold, completing an unfinished tap. Other keys, page
  focus loss, hiding, navigation and destruction stop or release the motion.
  Releases are observed at the native window as well as the page route, so
  continuous movement also works when OS auto-repeat is disabled.
- The last click or activated hint supplies a point, retained only for that
  primary page. If the first update at that point is not consumed, a new
  gesture tries the focused frame's centre.
- d/u use page-granularity compositor animation, adjusted for Chromium's paging
  fraction to cover half the actual container. They, gg and G are deliberate
  single jumps per press; OS repeats do not queue jumps. gg/G use document
  granularity to reach the ends of nested scrollers and preserve the horizontal
  offset.
- The old `0006-native-focused-page-scroll.patch` is removed. Scrolling no
  longer adds an API through WebContents, IPC, WebFrame and Blink. The new
  integration patch only registers Fly's source files in the browser build.

## Hint ownership

`ui/hint_controller.*` enables on-screen accessibility only while hints are
active. An assistive technology's pre-existing full tree stays intact. Screen
DIP bounds come from accessibility, including nested frames and zoom; no CSS
coordinates are reconstructed in the browser process.

Until a label is typed, tree updates can add targets and regenerate the
prefix-free code set, including late page hydration. Typing a prefix freezes
that set for the rest of the hint session, even after Backspace.
Existing labels then follow their target identities, hide when targets are
removed or off screen, and validate the target again before native activation.
Layout changes do not close the overlay. Navigation, page/window focus loss,
Escape and the browser hint shortcut retain their cancellation roles.

The overlay uses small bold capitals with a thin neutral border and faded typed
letters. Label views are reused when filtering or moving. `commands/hint_codes.*`
keeps the easiest labels as single letters until more prefixes are necessary.

## Limits and regression coverage

The activated scroll target is a hit-test point, not a DOM element reference.
A layout shift can move a different container under that point. A centre
fallback is also not Vimium's search for the largest scrollable element.
Scroll snapping, scroll chaining and smooth page jumps follow Chromium's
compositor policies. Hints only cover accessible clickable targets and retain
the existing target cap; they do not implement all Vimium features such as
link-text filtering, opening hints in background tabs or overlap rotation.
New targets added after you start typing require reopening hints. These are
explicit boundaries, not claims of complete Vimium parity.

Unit tests exercise tap distance, held-key speed, release, repeated taps,
frame-rate variation and prefix-free codes. Browser tests cover held scrolling
before any OS repeat, release and focus cancellation, real clicked containers,
half-container jumps at zoom, editable text entry, cross-origin frames,
recorder mappings, accumulated steps and late accessibility updates while a
hint prefix is typed. Changes to these contracts should extend those focused
native tests; `npm test` alone does not execute the C++ browser implementation.
