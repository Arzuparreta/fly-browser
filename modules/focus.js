// Keep Tab within the visible overlay while preserving each control's native behaviour.
export function initialiseOverlayFocus() {
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
