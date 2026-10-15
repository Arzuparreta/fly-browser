// Host and guest DOM timestamps use different time origins. Compare absolute
// event times, not IPC arrival times, so queued guest events cannot undo a newer
// host entry. Do not use :hover here: Electron can retain it over a guest view.
let lastHostEntry = -Infinity;

export function recordSidebarPointerEntry(event) {
    lastHostEntry = performance.timeOrigin + event.timeStamp;
}

export function isCurrentGuestPointerEntry(eventTime) {
    return Number.isFinite(eventTime) && eventTime >= lastHostEntry;
}
