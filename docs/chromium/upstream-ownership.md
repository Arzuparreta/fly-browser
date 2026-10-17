# Ownership findings at the pinned source

Inspected directly in the external checkout at the revision recorded in
`chromium/upstream.lock.json`. These are source findings, not compiled Fly
integration or evidence that M1 passes.

| Upstream source | Observed contract | Fly implication |
| --- | --- | --- |
| `chrome/browser/ui/browser_window/public/browser_window_interface.h` | `GetProfile()` is non-null, immutable for the desktop window lifetime, and all tabs share its BrowserContext | Keep a profile-bound controller per workspace; never change this profile on workspace switch |
| `components/tabs/public/tab_interface.h` | `IsActivated()`, `IsVisible()` and `IsSelected()` are distinct; content visibility is also distinct from widget occlusion | Reuse native split visibility; never derive visibility from command focus alone |
| `chrome/browser/ui/tabs/tab_strip_model.h` | `AddToNewSplit`, `UpdateSplitLayout`, `UpdateSplitRatio`, `ReverseTabsInSplit`, `RemoveSplit` and `RestoreSplit` already exist | Adapt Fly's two orientations and focused-pane actions to these operations within each workspace |
| `chrome/browser/ui/views/frame/multi_contents_view.h` | Owns two content containers, split layout and a distinct active index | Inspect and reuse native page layout; avoid building a second split state machine |
| `chrome/browser/ui/views/frame/browser_window_factory.cc` | Creates one `BrowserView` and `BrowserWidget` per browser controller | Single-window multi-workspace composition needs an explicit window adapter |
| `chrome/browser/ui/views/frame/browser_view.cc` | `AddedToWidget()` sets native-window properties for one BrowserView and one Profile | Simply adding several stock BrowserViews to one widget overwrites ownership lookup and is unsafe |
| `chrome/browser/extensions/extension_tab_util.cc` | Extension tab/window objects resolve through native browser/window controllers and session IDs | Preserve separate profile-local native registration; persistent Fly IDs cannot replace native extension IDs |
| `chrome/browser/ui/views/frame/browser_widget.h` | Widget, root view, platform frame, theme and keyboard routing refer to one BrowserView | Composition must cover activation, menus, popups, permissions, accessibility and platform teardown as well as content placement |

The next implementation gate is a native browser test proving the controller /
physical-window boundary with two Profiles. No topology adaptation has been
implemented or validated yet. Keep the unmodified control checkout clean until
it has built; use a separate external worktree for Fly integration.

Official source links are reproducible from the lock:
`https://chromium.googlesource.com/chromium/src/+/<revision>/<path>`.
