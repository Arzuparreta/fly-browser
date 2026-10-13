let surfaces = {};
let transitioning = false;

export const isOverlayTransition = () => transitioning;

export function hasActiveOverlay() {
    return Object.values(surfaces).some(surface => surface.active()) || !!document.querySelector('dialog[open]');
}

// Every opening path, including buttons and palette actions, uses the same exits.
export function prepareOverlay(id) {
    transitioning = true;
    try {
        for (const [otherId, surface] of Object.entries(surfaces)) {
            if (otherId !== id && surface.active()) surface.close();
        }
    } finally { transitioning = false; }
}

// Keep Tab within the visible overlay while preserving each control's native behaviour.
export function initialiseOverlayFocus(registeredSurfaces = {}) {
    surfaces = registeredSurfaces;
    window.addEventListener('keydown', event => {
        if (event.key !== 'Escape' || event.defaultPrevented || event.isComposing || document.querySelector('dialog[open]')) return;
        const entry = Object.entries(surfaces).find(([id, surface]) => surface.active() && document.getElementById(id)?.contains(document.activeElement));
        if (!entry) return;
        event.preventDefault(); event.stopImmediatePropagation(); entry[1].close();
    });
    const ids = ['PreferencesOverlay', 'CommandPaletteOverlay', 'DashboardOverlay', 'BookmarksOverlay', 'HistoryOverlay', 'NotesOverlay', 'DiagnosticsOverlay', 'ShareModalOverlay'];
    window.addEventListener('keydown', event => {
        if (event.key !== 'Tab' || document.querySelector('dialog[open]')) return;
        const overlays = ids.map(id => document.getElementById(id)).filter(root => root && root.getClientRects().length && getComputedStyle(root).visibility !== 'hidden');
        const root = overlays.find(overlay => overlay.contains(document.activeElement)) || overlays.at(-1);
        if (!root) return;
        const controls = [...root.querySelectorAll('button:not(:disabled), input:not(:disabled), textarea:not(:disabled), select:not(:disabled), a[href], [tabindex="0"]')].filter(control => control.getClientRects().length && getComputedStyle(control).visibility !== 'hidden');
        const first = controls[0], last = controls.at(-1);
        if (!first) { event.preventDefault(); root.focus(); return; }
        if (event.shiftKey && (document.activeElement === first || document.activeElement === root || !root.contains(document.activeElement))) {
            event.preventDefault(); last.focus();
        } else if (!event.shiftKey && (document.activeElement === last || document.activeElement === root || !root.contains(document.activeElement))) {
            event.preventDefault(); first.focus();
        }
    }, true);
}
