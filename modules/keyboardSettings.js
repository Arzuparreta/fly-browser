const keyboard = window.MiseKeyboard;
const clone = value => JSON.parse(JSON.stringify(value));
let saved, draft, defaults, metadata, recording, busy = false;
const el = id => document.getElementById(id);
const status = text => { el('KeyboardStatus').textContent = text; };
const dirty = () => JSON.stringify(draft) !== JSON.stringify(saved);
const scope = () => el('KeyboardScope').value;

function refreshButtons() {
    const validation = keyboard.validateConfig(draft, metadata);
    el('KeyboardSave').disabled = busy || !dirty() || !validation.ok;
    el('KeyboardDiscard').disabled = busy || !dirty();
    if (!validation.ok) status(validation.error);
    else status(dirty() ? 'Unsaved changes' : saved.warning || 'All changes saved');
}
function render() {
    if (!draft) return;
    const section = scope();
    const query = el('KeyboardSearch').value.toLowerCase().trim();
    const modifiedOnly = el('KeyboardModified').checked;
    const rows = el('KeyboardRows');
    rows.replaceChildren();
    let count = 0;
    for (const [id, info] of Object.entries(metadata).sort((a, b) => a[1].category.localeCompare(b[1].category) || a[1].label.localeCompare(b[1].label))) {
        const binds = draft[section][id] || [];
        const modified = JSON.stringify(binds) !== JSON.stringify(defaults[section][id] || []);
        if (modifiedOnly && !modified || query && !`${info.label} ${info.category} ${id} ${binds.join(' ')}`.toLowerCase().includes(query)) continue;
        count++;
        const row = document.createElement('div'); row.className = 'keyboard-row';
        const description = document.createElement('div');
        const label = document.createElement('strong'); label.textContent = info.label;
        const category = document.createElement('small'); category.textContent = info.category + (modified ? ' · Customised' : '');
        description.append(label, category);
        const controls = document.createElement('div'); controls.className = 'keyboard-bindings';
        binds.forEach((binding, index) => {
            const group = document.createElement('span'); group.className = 'keyboard-binding';
            const change = document.createElement('button'); change.type = 'button'; change.textContent = keyboard.display(binding, section);
            change.setAttribute('aria-label', `Change ${keyboard.display(binding, section)} for ${info.label}`);
            change.onclick = () => record(id, index);
            const remove = document.createElement('button'); remove.type = 'button'; remove.textContent = '×';
            remove.setAttribute('aria-label', `Remove ${keyboard.display(binding, section)} from ${info.label}`);
            remove.onclick = () => { draft[section][id].splice(index, 1); render(); focusAction(id); };
            group.append(change, remove); controls.append(group);
        });
        if (!binds.length) { const text = document.createElement('span'); text.className = 'keyboard-unbound'; text.textContent = 'Unassigned'; controls.append(text); }
        const add = document.createElement('button'); add.type = 'button'; add.textContent = 'Add binding';
        add.dataset.keyboardAction = id; add.setAttribute('aria-label', `Add binding for ${info.label}`);
        add.disabled = binds.length >= 8; add.onclick = () => record(id, null);
        const reset = document.createElement('button'); reset.type = 'button'; reset.textContent = 'Reset'; reset.disabled = !modified;
        reset.setAttribute('aria-label', `Reset bindings for ${info.label}`);
        reset.onclick = () => { draft[section][id] = [...defaults[section][id]]; render(); focusAction(id); };
        controls.append(add, reset); row.append(description, controls); rows.append(row);
    }
    el('KeyboardCount').textContent = `${count} ${count === 1 ? 'action' : 'actions'}`;
    if (!count) { const empty = document.createElement('p'); empty.textContent = 'No matching actions. Try another search or show all actions.'; rows.append(empty); }
    refreshButtons();
}
function focusAction(id) {
    const button = [...el('KeyboardRows').querySelectorAll('[data-keyboard-action]')].find(button => button.dataset.keyboardAction === id);
    if (button && !button.disabled) button.focus(); else el('KeyboardSearch').focus({preventScroll: true});
}
function record(id, index) {
    recording = {id, index, section: scope(), strokes: [], returnFocus: document.activeElement};
    el('KeyboardRecorderTitle').textContent = `Record: ${metadata[id].label}`;
    el('KeyboardSequenceLabel').hidden = scope() !== 'normal';
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
    const id = recording.id;
    draft = result.config;
    el('KeyboardRecorder').close(); recording = null; render(); focusAction(id);
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
        render();
    } catch (_) { status('Could not load keyboard settings. Reopen the browser to try again.'); return; }
    el('OpenKeyboardSettings').onclick = () => { el('KeyboardEditor').scrollIntoView({block: 'start'}); el('KeyboardSearch').focus({preventScroll: true}); };
    el('KeyboardSearch').oninput = render;
    el('KeyboardScope').onchange = render;
    el('KeyboardModified').onchange = render;
    el('KeyboardDiscard').onclick = () => { el('KeyboardSearch').focus({preventScroll: true}); draft = clone(saved); render(); };
    el('KeyboardReset').onclick = () => { draft[scope()] = clone(defaults[scope()]); render(); };
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
        const url = URL.createObjectURL(new Blob([JSON.stringify(draft, null, 2) + '\n'], {type: 'application/json'}));
        const link = document.createElement('a'); link.href = url; link.download = 'mise-keybinds.json'; link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
        status('Exported the current bindings, including unsaved changes.');
    };
    el('KeyboardImport').onchange = async event => {
        const file = event.target.files[0]; event.target.value = '';
        if (!file) return;
        try {
            if (file.size > 65536) throw new Error('The bindings file must be smaller than 64 KB.');
            const imported = JSON.parse(await file.text());
            const config = imported.version === 2 ? imported : {version: 2, shortcuts: imported, normal: {}};
            const result = keyboard.validateConfig({version: 2, shortcuts: {...defaults.shortcuts, ...config.shortcuts}, normal: {...defaults.normal, ...config.normal}}, metadata);
            if (!result.ok) throw new Error(result.error);
            draft = result.config; render(); status('Bindings imported. Review them, then save to apply.');
        } catch (error) { status(error.message); }
    };
}
