const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = file => fs.readFileSync(path.join(__dirname, '..', file), 'utf8').replace(/^import .*;\n/gm, '').replace(/^export /gm, '');
const handlers = {};
const roots = {HistoryOverlay: {contains: element => element === 'history'}, NotesOverlay: {contains: element => element === 'notes'}};
const context = {setTimeout: callback => callback(), window: {addEventListener: (type, callback, capture) => {if (!capture) handlers[type] = callback;}}, document: {querySelector: () => null, getElementById: id => roots[id], activeElement: 'history'}};
vm.createContext(context); vm.runInContext(source('modules/focus.js') + '\nthis.isOverlayTransition = isOverlayTransition;', context);
let history = true, notes = false;
context.initialiseOverlayFocus({
    HistoryOverlay: {active: () => history, close: () => {history = false;}},
    NotesOverlay: {active: () => notes, close: () => {assert(context.isOverlayTransition()); notes = false;}}
});
const escape = () => ({key: 'Escape', preventDefault() {this.defaultPrevented = true;}, stopImmediatePropagation() {}});
const event = escape(); handlers.keydown(event); assert(event.defaultPrevented); assert(!history);
history = true; notes = true; context.prepareOverlay('HistoryOverlay'); assert(history); assert(!notes); assert(!context.isOverlayTransition());
context.document.querySelector = () => ({}); handlers.keydown(escape()); assert(history, 'Native dialogs retain cancellation ownership');

// Pointer-down runs before action handlers, including controls that stop click bubbling.
const pointerHandlers = {};
const control = {blur() {context.document.activeElement = null;}};
const sidebar = {contains: el => el === control, addEventListener(type, callback, capture) {assert(capture); pointerHandlers[type] = callback;}};
let pageFocus = 0;
context.initialiseSidebarPointerFocus(sidebar, () => pageFocus++);
const pointer = (editable = false, form = false) => ({target: {isContentEditable: editable, closest: selector => selector === 'input, textarea, select' ? (form ? {} : null) : control}, preventDefault() {this.defaultPrevented = true;}});
context.document.querySelector = () => null;
history = false; context.document.activeElement = control;
let down = pointer(); pointerHandlers.mousedown(down);
assert(down.defaultPrevented); assert.equal(context.document.activeElement, null); assert.equal(pageFocus, 0);
pointerHandlers.click({detail: 1}); assert.equal(pageFocus, 1);
pointerHandlers.click({detail: 0}); assert.equal(pageFocus, 1);
history = true; context.document.activeElement = 'history';
down = pointer(); pointerHandlers.mousedown(down);
pointerHandlers.click({detail: 1});
assert(down.defaultPrevented); assert.equal(context.document.activeElement, 'history'); assert.equal(pageFocus, 1, 'Pointer actions must preserve an active overlay');
for (const down of [pointer(true), pointer(false, true)]) {
    pointerHandlers.mousedown(down); assert(!down.defaultPrevented); assert.equal(pageFocus, 1);
}
console.log('Shared sidebar pointer policy preserves overlays and native editing.');

(async () => {
    const elements = {};
    const events = {}, pending = [], saves = [], timers = [];
    for (const id of ['NotesOverlay', 'NotesTextArea', 'NotesMarkdownView', 'ToggleEditNotesBtn', 'CloseNotesBtn']) {
        elements[id] = {style: {}, value: '', focus() {}, addEventListener(type, callback) {events[id + type] = callback;}};
    }
    const notesContext = {
        state: {notesActive: false}, document: {getElementById: id => elements[id]},
        window: {addEventListener() {}, flyAPI: {readNotes: () => new Promise(resolve => pending.push(resolve)), saveNotes: value => saves.push(value)}},
        prepareOverlay() {}, focusActiveWebview() {}, escapeHtml: text => text, console,
        setTimeout: callback => {timers.push(callback);}, clearTimeout() {}
    };
    vm.createContext(notesContext); vm.runInContext(source('modules/overlays/notes.js'), notesContext);
    notesContext.setupNotesListeners();
    notesContext.toggleNotesOverlay(); notesContext.toggleNotesOverlay();
    pending.shift()('Existing notes'); await Promise.resolve();
    assert.equal(elements.NotesOverlay.style.display, 'none'); assert.deepEqual(saves, [], 'Closing before reading must not overwrite existing notes');
    notesContext.toggleNotesOverlay(); notesContext.toggleNotesOverlay(); notesContext.toggleNotesOverlay();
    pending.shift()('Stale notes'); await Promise.resolve(); assert.equal(elements.NotesTextArea.value, '');
    pending.shift()('Fresh notes'); await Promise.resolve(); assert.equal(elements.NotesTextArea.value, 'Fresh notes');
    notesContext.toggleNotesOverlay(); notesContext.toggleNotesOverlay();
    elements.NotesTextArea.value = 'Typed while loading'; events.NotesTextAreainput({target: elements.NotesTextArea});
    pending.shift()('Old disk content'); await Promise.resolve(); assert.equal(elements.NotesTextArea.value, 'Typed while loading');
    notesContext.toggleNotesOverlay(); assert.equal(saves.at(-1), 'Typed while loading');
    // Exercise the real palette open/close functions with deferred configuration reads.
    const paletteSource = source('modules/overlays/commandPalette.js');
    const palettePending = [], paletteFilters = [];
    const paletteInput = {value: '', focus() {}};
    const paletteOverlay = {style: {}};
    const paletteContext = {
        state: {paletteActive: false}, document: {getElementById: id => id === 'PaletteInput' ? paletteInput : id === 'CommandPaletteOverlay' ? paletteOverlay : null},
        prepareOverlay() {}, returnFocusToWebview() {pageFocus++;},
        loadDynamicKeybinds: () => new Promise(resolve => palettePending.push(resolve)),
        prepareExtensionPaletteCommands: async () => {}, filterPaletteCommands: query => paletteFilters.push(query)
    };
    vm.createContext(paletteContext);
    vm.runInContext('let paletteRequest = 0; let activeCategoryFilter = "all";\n' + paletteSource.slice(paletteSource.indexOf('function closeCommandPalette('), paletteSource.indexOf('function renderPaletteInspector(')), paletteContext);
    let opening = paletteContext.openCommandPalette();
    paletteContext.closeCommandPalette(); palettePending.shift()(); await opening;
    assert.equal(paletteOverlay.style.display, 'none'); assert(!paletteContext.state.paletteActive);
    opening = paletteContext.openCommandPalette(); paletteInput.value = 'Typed during preparation';
    palettePending.shift()(); await opening;
    assert.equal(paletteInput.value, 'Typed during preparation'); assert.equal(paletteFilters.at(-1), paletteInput.value);
    paletteContext.closeCommandPalette();
    const staleOpening = paletteContext.openCommandPalette(); paletteContext.closeCommandPalette();
    opening = paletteContext.openCommandPalette(); paletteInput.value = 'Current opening';
    const previousFilterCount = paletteFilters.length;
    palettePending.shift()(); await staleOpening; assert.equal(paletteFilters.length, previousFilterCount);
    palettePending.shift()(); await opening; assert.equal(paletteFilters.at(-1), 'Current opening');
    // Selecting an overlay command must not focus a guest between the two panels.
    vm.runInContext(paletteSource.slice(paletteSource.indexOf('function executePaletteSelection('), paletteSource.indexOf('function setupCommandPaletteOverlayListeners(')), paletteContext);
    paletteContext.recordRecentCommand = () => {};
    paletteContext.state.paletteMatches = [{id: 'settings', action() {paletteContext.state.preferencesActive = true;}}];
    paletteContext.state.paletteSelectionIdx = 0;
    paletteContext.state.paletteActive = true;
    const focusBefore = pageFocus;
    paletteContext.executePaletteSelection('settings');
    assert.equal(pageFocus, focusBefore, 'An overlay command keeps guest focus untouched');
    assert(paletteContext.state.preferencesActive);
    paletteContext.state.preferencesActive = false;
    paletteContext.state.paletteActive = true;
    paletteContext.state.paletteMatches = [{id: 'copy', action() {}}];
    paletteContext.executePaletteSelection('copy');
    assert.equal(pageFocus, focusBefore + 1, 'A non-overlay command returns page focus once');
    console.log('Overlay cancellation ownership, transition focus suppression, delayed notes and palette races passed.');
})().catch(error => {console.error(error); process.exitCode = 1;});
