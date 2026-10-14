const keyboard = window.MiseKeyboard;
const clone = value => JSON.parse(JSON.stringify(value));
let saved, draft, defaults, metadata, recording, busy = false;
const el = id => document.getElementById(id);
const status = text => { el('KeyboardStatus').textContent = text; };
const dirty = () => JSON.stringify(draft) !== JSON.stringify(saved);
const sections = {shortcuts: 'Browser shortcuts', normal: 'Page keys'};
const expanded = new Set();
let editorRequested = false;

// Keep the action catalogue available to search, but build rows only on demand.
export function showKeyboardEditor() {
    if (editorRequested) return;
    editorRequested = true;
    render();
}

export function hideKeyboardEditor() {
    if (!editorRequested) return;
    editorRequested = false;
    el('KeyboardRows').replaceChildren();
}

function refreshButtons() {
    const validation = keyboard.validateConfig(draft, metadata);
    el('KeyboardSave').disabled = busy || !dirty() || !validation.ok;
    el('KeyboardDiscard').disabled = busy || !dirty();
    if (!validation.ok) status(validation.error);
    else status(dirty() ? 'Unsaved changes' : saved.warning || 'All changes saved');
}
function matchesQuery(id, info, query) {
    if (!query) return true;
    const allBindings = Object.keys(sections).flatMap(section => (draft[section][id] || []).map(binding => keyboard.display(binding, section)));
    return `${info.label} ${info.category} ${id} ${allBindings.join(' ')}`.toLowerCase().includes(query);
}
// Used by the Preferences search to report matching actions without rendering them.
export function keyboardMatchCount(query) {
    if (!draft) return 0;
    query = query.toLowerCase().trim();
    return Object.entries(metadata).filter(([id, info]) => matchesQuery(id, info, query)).length;
}
export function filterKeyboardActions(query) {
    el('KeyboardSearch').value = query;
    el('KeyboardModified').checked = false;
    render();
}
function render() {
    if (!draft || !editorRequested) return;
    const query = el('KeyboardSearch').value.toLowerCase().trim();
    const modifiedOnly = el('KeyboardModified').checked;
    const rows = el('KeyboardRows');
    rows.replaceChildren();
    let count = 0;
    for (const [id, info] of Object.entries(metadata).sort((a, b) => a[1].category.localeCompare(b[1].category) || a[1].label.localeCompare(b[1].label))) {
        const modified = Object.keys(sections).some(section => JSON.stringify(draft[section][id]) !== JSON.stringify(defaults[section][id]));
        if (modifiedOnly && !modified || !matchesQuery(id, info, query)) continue;
        count++;
        const row = document.createElement('div'); row.className = 'keyboard-row';
        const description = document.createElement('div');
        const label = document.createElement('strong'); label.textContent = info.label;
        const category = document.createElement('small'); category.textContent = info.category + (modified ? ' · Customised' : '');
        description.append(label, category);
        const menus = document.createElement('div'); menus.className = 'keyboard-menus';
        for (const [section, title] of Object.entries(sections)) {
            const binds = draft[section][id] || [];
            const modified = JSON.stringify(binds) !== JSON.stringify(defaults[section][id] || []);
            const menu = document.createElement('details'); menu.dataset.keyboardSection = section;
            const menuKey = `${id}:${section}`; menu.open = expanded.has(menuKey);
            const summary = document.createElement('summary');
            summary.textContent = `${title}: ${binds.length ? binds.map(binding => keyboard.display(binding, section)).join(' / ') : 'Unassigned'}`;
            menu.append(summary);
            const populate = () => {
                if (menu.childElementCount > 1) return;
                const help = document.createElement('p'); help.className = 'keyboard-help';
                help.textContent = section === 'shortcuts' ? 'Works while typing too.' : 'Works outside editable fields, INSERT mode, hints and passthrough. Browser panels keep their own controls.';
                const controls = document.createElement('div'); controls.className = 'keyboard-bindings';
                binds.forEach((binding, index) => {
                    const group = document.createElement('span'); group.className = 'keyboard-binding';
                    const change = document.createElement('button'); change.type = 'button'; change.textContent = keyboard.display(binding, section);
                    change.setAttribute('aria-label', `Change ${keyboard.display(binding, section)} for ${info.label}`);
                    change.onclick = () => record(id, index, section);
                    const remove = document.createElement('button'); remove.type = 'button'; remove.textContent = '×';
                    remove.setAttribute('aria-label', `Remove ${keyboard.display(binding, section)} from ${info.label}`);
                    remove.onclick = () => { draft[section][id].splice(index, 1); render(); focusAction(id, section); };
                    group.append(change, remove); controls.append(group);
                });
                if (!binds.length) { const text = document.createElement('span'); text.className = 'keyboard-unbound'; text.textContent = 'Unassigned'; controls.append(text); }
                const add = document.createElement('button'); add.type = 'button'; add.textContent = 'Add binding';
                add.dataset.keyboardAction = id; add.dataset.keyboardScope = section; add.setAttribute('aria-label', `Add binding for ${info.label}`);
                add.disabled = binds.length >= 8; add.onclick = () => record(id, null, section);
                const reset = document.createElement('button'); reset.type = 'button'; reset.textContent = 'Reset'; reset.disabled = !modified;
                reset.setAttribute('aria-label', `Reset bindings for ${info.label}`);
                reset.onclick = () => { draft[section][id] = [...defaults[section][id]]; render(); focusAction(id, section); };
                controls.append(add, reset); menu.append(help, controls);
            };
            if (menu.open) populate();
            menu.ontoggle = () => {
                if (!menu.isConnected) return;
                if (menu.open) { expanded.add(menuKey); populate(); }
                else { expanded.delete(menuKey); while (menu.childElementCount > 1) menu.lastElementChild.remove(); }
            };
            menus.append(menu);
        }
        row.append(description, menus); rows.append(row);
    }
    el('KeyboardCount').textContent = `${count} ${count === 1 ? 'action' : 'actions'}`;
    if (!count) { const empty = document.createElement('p'); empty.textContent = 'No matching actions. Try another search or show all actions.'; rows.append(empty); }
    refreshButtons();
}
function focusAction(id, section) {
    const button = [...el('KeyboardRows').querySelectorAll('[data-keyboard-action]')].find(button => button.dataset.keyboardAction === id && button.dataset.keyboardScope === section);
    if (button && !button.disabled) button.focus(); else el('KeyboardSearch').focus({preventScroll: true});
}
function record(id, index, section) {
    recording = {id, index, section, strokes: [], returnFocus: document.activeElement};
    el('KeyboardRecorderTitle').textContent = `Record ${sections[section]}: ${metadata[id].label}`;
    el('KeyboardSequenceLabel').hidden = section !== 'normal';
    el('KeyboardSequence').checked = false;
    el('KeyboardRecorded').textContent = 'Waiting for a key…';
    el('KeyboardRecordError').textContent = '';
    el('KeyboardRecordUse').disabled = true;
    el('KeyboardRecordReplace').hidden = true;
    el('KeyboardRecorder').showModal();
    el('KeyboardRecorded').focus();
}
function candidate(reassign = false) {
    const next = clone(draft);
    const {id, section, index, strokes} = recording;
    const binding = strokes.join(' ');
    if (reassign) for (const table of ['shortcuts', 'normal']) {
        for (const action of Object.keys(next[table])) {
            next[table][action] = next[table][action].filter(value => {
                if (table === section) return value !== binding && !value.startsWith(binding + ' ') && !binding.startsWith(value + ' ');
                return table === 'shortcuts' ? !strokes.includes(value) : !value.split(' ').includes(binding);
            });
        }
    }
    // Remove the old assignment before conflict reassignment can change its index.
    if (index !== null) {
        const old = draft[section][id][index];
        next[section][id] = next[section][id].filter(value => value !== old);
    }
    if (!next[section][id].includes(binding)) next[section][id].push(binding);
    return keyboard.validateConfig(next, metadata);
}
function finishRecord(reassign) {
    const result = candidate(reassign);
    if (!result.ok) { el('KeyboardRecordError').textContent = result.error; return; }
    const {id, section} = recording;
    draft = result.config;
    el('KeyboardRecorder').close(); recording = null; render(); focusAction(id, section);
}
function cancelRecord() {
    const focus = recording?.returnFocus;
    recording = null; el('KeyboardRecorder').close(); focus?.focus();
}
export async function initialiseKeyboardEditor() {
    try {
        [saved, defaults, metadata] = await Promise.all([window.miseAPI.getKeyboardConfig(), window.miseAPI.getKeyboardDefaults(), window.miseAPI.getActionMetadata()]);
        defaults = keyboard.validateConfig(defaults, metadata).config;
        draft = clone(saved);
        if (editorRequested) render(); else refreshButtons();
    } catch (_) { status('Could not load keyboard settings. Reopen the browser to try again.'); return; }
    el('KeyboardSearch').oninput = render;
    el('KeyboardModified').onchange = render;
    el('KeyboardDiscard').onclick = () => { el('KeyboardSearch').focus({preventScroll: true}); draft = clone(saved); render(); };
    for (const section of Object.keys(sections)) el(section === 'shortcuts' ? 'KeyboardResetShortcuts' : 'KeyboardResetNormal').onclick = () => { draft[section] = clone(defaults[section]); render(); };
    el('KeyboardSave').onclick = async () => {
        if (busy) return;
        el('KeyboardSearch').focus({preventScroll: true});
        busy = true; refreshButtons(); status('Saving…');
        const snapshot = clone(draft);
        try {
            const result = await window.miseAPI.saveKeybinds(snapshot);
            if (!result.ok) { status(result.error); return; }
            saved = clone(result.config);
            if (JSON.stringify(draft) === JSON.stringify(snapshot)) draft = clone(saved);
            status(dirty() ? 'Saved. You have further unsaved changes.' : 'Bindings saved and applied to all tabs.');
        } catch (_) { status('Could not save bindings. Your changes are still available here.'); }
        finally {
            busy = false;
            el('KeyboardSave').disabled = !dirty() || !keyboard.validateConfig(draft, metadata).ok;
            el('KeyboardDiscard').disabled = !dirty();
        }
    };
    el('KeyboardRecordUse').onclick = () => finishRecord(false);
    el('KeyboardRecordReplace').onclick = () => finishRecord(true);
    el('KeyboardRecordCancel').onclick = cancelRecord;
    el('KeyboardRecorder').addEventListener('cancel', event => { event.preventDefault(); cancelRecord(); });
    el('KeyboardSequence').onchange = () => { recording.strokes = []; el('KeyboardRecorded').textContent = 'Waiting for a key…'; el('KeyboardRecordUse').disabled = true; el('KeyboardRecordReplace').hidden = true; };
    // Capture before the app's dispatcher so recording Ctrl+W cannot close a tab.
    window.addEventListener('keydown', event => {
        if (!recording) return;
        if (event.key === 'Escape') { event.preventDefault(); event.stopImmediatePropagation(); cancelRecord(); return; }
        // Keep native keyboard access to the dialog's controls.
        if (event.key === 'Tab' || (event.key === 'Enter' || event.key === ' ') && event.target.tagName === 'BUTTON') return;
        event.preventDefault(); event.stopImmediatePropagation();
        if (event.repeat || event.isComposing) return;
        const combo = keyboard.eventCombo(event);
        if (!combo) return;
        const length = el('KeyboardSequence').checked ? 2 : 1;
        if (recording.strokes.length === length) recording.strokes = [];
        recording.strokes.push(combo);
        el('KeyboardRecorded').textContent = keyboard.display(recording.strokes.join(' '), recording.section) + (recording.strokes.length < length ? ' …' : '');
        if (recording.strokes.length < length) { el('KeyboardRecordUse').disabled = true; return; }
        const result = candidate();
        const reassigned = candidate(true);
        let explanation = result.ok ? '' : result.error;
        if (!result.ok && reassigned.ok) {
            const affected = new Set();
            for (const section of ['shortcuts', 'normal']) for (const [id, binds] of Object.entries(draft[section])) {
                if (id !== recording.id && binds.some(binding => !reassigned.config[section][id].includes(binding))) affected.add(metadata[id].label);
            }
            if (affected.size) explanation += ` Reassigning will remove the conflicting bindings from: ${[...affected].join(', ')}.`;
        }
        el('KeyboardRecordError').textContent = explanation;
        el('KeyboardRecordUse').disabled = !result.ok;
        el('KeyboardRecordReplace').hidden = result.ok || !reassigned.ok;
    }, true);
    el('KeyboardExport').onclick = () => {
        // Export only changes so that importing later still picks up newer defaults.
        const changes = {version: 2, changesOnly: true, ...keyboard.changesFrom(draft, keyboard.validateConfig(defaults, metadata).config)};
        const url = URL.createObjectURL(new Blob([JSON.stringify(changes, null, 2) + '\n'], {type: 'application/json'}));
        const link = document.createElement('a'); link.href = url; link.download = 'mise-keybinds.json'; link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
        status('Exported the current bindings, including unsaved changes.');
    };
    el('KeyboardImport').onchange = async event => {
        const file = event.target.files[0]; event.target.value = '';
        if (!file) return;
        try {
            if (file.size > 65536) throw new Error('The bindings file must be smaller than 64 KB.');
            const result = await window.miseAPI.resolveKeybinds(JSON.parse(await file.text()));
            if (!result.ok) throw new Error(result.error);
            draft = result.config; render(); status('Bindings imported. Review them, then save to apply.');
        } catch (error) { status(error.message); }
    };
}
