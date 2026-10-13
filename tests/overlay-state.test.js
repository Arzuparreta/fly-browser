const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = file => fs.readFileSync(path.join(__dirname, '..', file), 'utf8').replace(/^import .*;\n/gm, '').replace(/^export /gm, '');
const handlers = {};
const roots = {HistoryOverlay: {contains: element => element === 'history'}, NotesOverlay: {contains: element => element === 'notes'}};
const context = {window: {addEventListener: (type, callback, capture) => {if (!capture) handlers[type] = callback;}}, document: {querySelector: () => null, getElementById: id => roots[id], activeElement: 'history'}};
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

(async () => {
    const elements = {};
    const events = {}, pending = [], saves = [], timers = [];
    for (const id of ['NotesOverlay', 'NotesTextArea', 'NotesMarkdownView', 'ToggleEditNotesBtn', 'CloseNotesBtn']) {
        elements[id] = {style: {}, value: '', focus() {}, addEventListener(type, callback) {events[id + type] = callback;}};
    }
    const notesContext = {
        state: {notesActive: false}, document: {getElementById: id => elements[id]},
        window: {miseAPI: {readNotes: () => new Promise(resolve => pending.push(resolve)), saveNotes: value => saves.push(value)}},
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
        prepareOverlay() {}, returnFocusToWebview() {},
        loadDynamicKeybinds: () => new Promise(resolve => palettePending.push(resolve)),
        prepareExtensionPaletteCommands: async () => {}, filterPaletteCommands: query => paletteFilters.push(query)
    };
    vm.createContext(paletteContext);
    vm.runInContext('let paletteRequest = 0; let activeCategoryFilter = "all";\n' + paletteSource.slice(paletteSource.indexOf('function closeCommandPalette()'), paletteSource.indexOf('function renderPaletteInspector(')), paletteContext);
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
    console.log('Overlay cancellation ownership, transition focus suppression, delayed notes and palette races passed.');
})().catch(error => {console.error(error); process.exitCode = 1;});
