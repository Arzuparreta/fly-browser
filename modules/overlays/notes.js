import { prepareOverlay } from '../focus.js';
import { state } from '../state.js';
import { escapeHtml, focusActiveWebview } from '../utils.js';

let notesSaveTimeout = null;
let notesRequest = 0;
let notesLoaded = false;
let notesEdited = false;

// Simple inline markdown parser helper
export function parseMarkdownToHtml(text) {
    if (!text) return '';
    
    let html = escapeHtml(text);

    html = html.replace(/^### (.*$)/gim, '<h3>$1</h3>');
    html = html.replace(/^## (.*$)/gim, '<h2>$1</h2>');
    html = html.replace(/^# (.*$)/gim, '<h1>$1</h1>');

    html = html.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>');
    html = html.replace(/\*(.*?)\*/g, '<em>$1</em>');
    html = html.replace(/`(.*?)`/g, '<code>$1</code>');

    html = html.replace(/^\&gt\; (.*$)/gim, '<blockquote>$1</blockquote>');
    html = html.replace(/^> (.*$)/gim, '<blockquote>$1</blockquote>');
    html = html.replace(/^\- (.*$)/gim, '<li>$1</li>');

    html = html.replace(/\n/g, '<br>');

    return html;
}

export function renderNotesView() {
    const textarea = document.getElementById('NotesTextArea');
    const mdView = document.getElementById('NotesMarkdownView');
    const editBtn = document.getElementById('ToggleEditNotesBtn');

    if (!textarea || !mdView) return;

    if (state.isEditingNotes) {
        textarea.style.display = 'block';
        mdView.style.display = 'none';
        if (editBtn) editBtn.textContent = 'Preview';
        setTimeout(() => { if (state.notesActive && state.isEditingNotes) textarea.focus(); }, 50);
    } else {
        mdView.innerHTML = parseMarkdownToHtml(textarea.value);
        textarea.style.display = 'none';
        mdView.style.display = 'block';
        if (editBtn) { editBtn.textContent = 'Edit'; if (state.notesActive) editBtn.focus(); }
    }
}

export function toggleNotesOverlay() {
    const overlay = document.getElementById('NotesOverlay');
    const textarea = document.getElementById('NotesTextArea');

    if (!overlay || !textarea) return;

    const request = ++notesRequest;
    state.notesActive = !state.notesActive;

    if (state.notesActive) {
        prepareOverlay('NotesOverlay');
        notesLoaded = false;
        notesEdited = false;
        overlay.style.display = 'flex';
        document.getElementById('CloseNotesBtn')?.focus();
        if (window.miseAPI && typeof window.miseAPI.readNotes === 'function') {
            window.miseAPI.readNotes().then((content) => {
                if (!state.notesActive || request !== notesRequest || notesEdited) return;
                notesLoaded = true;
                textarea.value = content || '';
                state.isEditingNotes = false;
                renderNotesView();
            }).catch((err) => {
                console.error("Failed to load notes:", err);
            });
        } else {
            overlay.style.display = 'flex';
        }
    } else {
        clearTimeout(notesSaveTimeout);
        if (notesLoaded && window.miseAPI && typeof window.miseAPI.saveNotes === 'function') {
            window.miseAPI.saveNotes(textarea.value);
        }
        overlay.style.display = 'none';
        focusActiveWebview();
    }
}

export function setupNotesListeners() {
    const textarea = document.getElementById('NotesTextArea');
    const closeBtn = document.getElementById('CloseNotesBtn');
    const editBtn = document.getElementById('ToggleEditNotesBtn');

    if (editBtn) {
        editBtn.addEventListener('click', () => {
            state.isEditingNotes = !state.isEditingNotes;
            renderNotesView();
        });
    }

    if (textarea) {
        textarea.addEventListener('input', (e) => {
            notesLoaded = true;
            notesEdited = true;
            const content = e.target.value;
            clearTimeout(notesSaveTimeout);
            notesSaveTimeout = setTimeout(() => {
                if (window.miseAPI && typeof window.miseAPI.saveNotes === 'function') {
                    window.miseAPI.saveNotes(content);
                }
            }, 300);
        });

        textarea.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                toggleNotesOverlay();
            }
        });
    }

    if (closeBtn) {
        closeBtn.addEventListener('click', toggleNotesOverlay);
    }
}
