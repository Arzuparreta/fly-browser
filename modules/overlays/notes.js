import { prepareOverlay } from '../focus.js';
import { state } from '../state.js';
import { escapeHtml, focusActiveWebview } from '../utils.js';
import { spawnTabWithUrl } from '../webview.js';

let notesSaveTimeout = null;
let notesRequest = 0;
let notesLoaded = false;
let notesEdited = false;

// Lightweight inline markdown parser
export function parseMarkdownToHtml(text) {
    if (!text) return '';
    
    let html = escapeHtml(text);

    // Block elements: headers and dividers
    html = html.replace(/^(\*{3,}|_{3,})$/gim, '<hr>');
    html = html.replace(/^### (.*$)/gim, '<h3>$1</h3>');
    html = html.replace(/^## (.*$)/gim, '<h2>$1</h2>');
    html = html.replace(/^# (.*$)/gim, '<h1>$1</h1>');

    // Inline styling
    html = html.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>');
    html = html.replace(/\*(.*?)\*/g, '<em>$1</em>');
    html = html.replace(/`(.*?)`/g, '<code>$1</code>');

    // Blockquotes and lists
    html = html.replace(/^\&gt\; (.*$)/gim, '<blockquote>$1</blockquote>');
    html = html.replace(/^> (.*$)/gim, '<blockquote>$1</blockquote>');
    html = html.replace(/^\- (.*$)/gim, '<li>$1</li>');

    // Hyperlinks [label](url)
    html = html.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');

    html = html.replace(/\n/g, '<br>');
    html = html.replace(/<\/blockquote><br><blockquote>/g, '<br>');

    return html;
}

// Word and character count indicator
export function updateWordCountBadge(text) {
    if (typeof document === 'undefined') return;
    const badge = document.getElementById('NotesWordCount');
    if (!badge) return;
    const trimmed = (text || '').trim();
    if (!trimmed) {
        badge.textContent = '0 words';
        badge.title = '0 words, 0 characters';
        return;
    }
    const words = trimmed.split(/\s+/).filter(Boolean).length;
    const chars = trimmed.length;
    badge.textContent = `${words} word${words === 1 ? '' : 's'}`;
    badge.title = `${words} words, ${chars} characters`;
}

export function renderNotesView() {
    if (typeof document === 'undefined') return;
    const textarea = document.getElementById('NotesTextArea');
    const mdView = document.getElementById('NotesMarkdownView');
    const editBtn = document.getElementById('ToggleEditNotesBtn');

    if (!textarea || !mdView) return;

    updateWordCountBadge(textarea.value);

    if (state.isEditingNotes) {
        textarea.style.display = 'block';
        mdView.style.display = 'none';
        if (editBtn) editBtn.innerHTML = '<i class="fa-solid fa-eye"></i> Preview';
        setTimeout(() => {
            if (!state.notesActive || !state.isEditingNotes) return;
            textarea.focus();
            const len = textarea.value.length;
            textarea.setSelectionRange(len, len);
        }, 30);
    } else {
        mdView.innerHTML = parseMarkdownToHtml(textarea.value);
        textarea.style.display = 'none';
        mdView.style.display = 'block';
        if (editBtn) editBtn.innerHTML = '<i class="fa-solid fa-pen-to-square"></i> Edit';
        setTimeout(() => {
            if (state.notesActive && !state.isEditingNotes) mdView.focus();
        }, 30);
    }
}

export function toggleExpandNotesOverlay() {
    if (typeof document === 'undefined') {
        state.isNotesExpanded = !state.isNotesExpanded;
        return;
    }
    const content = document.getElementById('NotesContent');
    const expandBtn = document.getElementById('ToggleExpandNotesBtn');
    if (!content) return;

    state.isNotesExpanded = !state.isNotesExpanded;
    content.classList.toggle('notes-expanded', state.isNotesExpanded);

    if (expandBtn) {
        expandBtn.innerHTML = state.isNotesExpanded
            ? '<i class="fa-solid fa-compress"></i>'
            : '<i class="fa-solid fa-expand"></i>';
        expandBtn.title = state.isNotesExpanded ? 'Restore Normal Size' : 'Expand / Fullscreen';
    }
}

export async function openNotesInExternalApp() {
    if (typeof window !== 'undefined' && window.miseAPI && typeof window.miseAPI.openNotesInExternalApp === 'function') {
        const textarea = typeof document !== 'undefined' ? document.getElementById('NotesTextArea') : null;
        if (textarea && typeof window.miseAPI.saveNotes === 'function') {
            await window.miseAPI.saveNotes(textarea.value);
        }
        const res = await window.miseAPI.openNotesInExternalApp();
        if (res && res.success) {
            const appName = res.app === 'system-default' ? 'default Markdown editor' : res.app;
            if (typeof window.setTargetUrl === 'function') {
                window.setTargetUrl(`Opened Quick Notes in ${appName}`, 3000);
            }
        } else if (typeof window.setTargetUrl === 'function') {
            window.setTargetUrl(`Failed to open notes in external app: ${res?.error || 'Unknown error'}`, 3000);
        }
    }
}

// Convert distilled article or web clipping into clean Markdown
export function formatArticleToMarkdown(articleData) {
    const title = (articleData.title || 'Untitled Article').trim();
    const url = (articleData.url || '').trim();
    const byline = (articleData.byline || '').trim();
    const site = (articleData.siteName || '').trim();
    const dateStr = new Date().toISOString().replace('T', ' ').slice(0, 16);

    const metaParts = [];
    if (site) metaParts.push(`*Source:* ${site}`);
    if (url) metaParts.push(`[Original Link](${url})`);
    metaParts.push(`*Captured:* ${dateStr}`);
    if (byline) metaParts.push(`*Author:* ${byline}`);

    let md = `\n\n***\n\n## [${title}](${url || '#'})\n`;
    if (metaParts.length > 0) {
        md += `${metaParts.join(' · ')}\n\n`;
    }

    if (articleData.excerpt) {
        md += `> ${articleData.excerpt.trim()}\n\n`;
    }

    if (articleData.textContent) {
        const paragraphs = articleData.textContent
            .split(/\n{2,}/)
            .map(p => p.trim())
            .filter(p => p.length > 0);
        md += paragraphs.join('\n\n') + '\n';
    }

    return md;
}

// Append article clipping to quick notes
export async function exportArticleToNotes(articleData) {
    if (!articleData) return;
    const markdownSnippet = formatArticleToMarkdown(articleData);
    const textarea = document.getElementById('NotesTextArea');

    let currentNotes = '';
    if (window.miseAPI && typeof window.miseAPI.readNotes === 'function') {
        try {
            currentNotes = (await window.miseAPI.readNotes()) || '';
        } catch (e) {
            currentNotes = textarea ? textarea.value : '';
        }
    } else if (textarea) {
        currentNotes = textarea.value;
    }

    const newContent = currentNotes.trim()
        ? `${currentNotes.trim()}${markdownSnippet}`
        : markdownSnippet.trim();

    if (textarea) {
        textarea.value = newContent;
        renderNotesView();
    }

    if (window.miseAPI && typeof window.miseAPI.saveNotes === 'function') {
        await window.miseAPI.saveNotes(newContent);
    }

    const title = articleData.title || 'Article';
    const shortTitle = title.length > 35 ? `${title.slice(0, 32)}...` : title;
    if (typeof window.setTargetUrl === 'function') {
        window.setTargetUrl(`Exported "${shortTitle}" to Quick Notes (~/.config/mise-browser/notes.md)`, 3500);
    }
}

// Export clipping to standalone file in external note-taking tool
export async function exportArticleToExternalApp(articleData) {
    if (!articleData) return;
    const markdownSnippet = formatArticleToMarkdown(articleData);
    if (window.miseAPI && typeof window.miseAPI.exportArticleToExternal === 'function') {
        const res = await window.miseAPI.exportArticleToExternal({
            title: articleData.title || 'Article',
            markdown: markdownSnippet.trim()
        });
        const appName = res && res.app === 'system-default' ? 'default Markdown editor' : res?.app || 'external editor';
        if (typeof window.setTargetUrl === 'function') {
            window.setTargetUrl(`Exported clipping to ${appName}`, 3500);
        }
    }
}

// Convert highlighted snippet or quote into clean Markdown
export function formatSelectionToMarkdown(selectionData) {
    if (!selectionData || !selectionData.text) return '';
    const text = selectionData.text.trim();
    if (!text) return '';

    const title = (selectionData.title || 'Web clipping').trim();
    const url = (selectionData.url || '').trim();
    const dateStr = new Date().toISOString().replace('T', ' ').slice(0, 16);

    let md = `\n\n***\n\n`;
    if (url) {
        md += `### [${title}](${url})\n`;
    } else {
        md += `### ${title}\n`;
    }
    md += `*Captured: ${dateStr}*\n\n`;

    const quoteLines = text
        .split('\n')
        .map(line => line.trim() ? `> ${line.trim()}` : '>')
        .join('\n');

    md += `${quoteLines}\n`;
    return md;
}

// Append highlighted selection to quick notes
export async function appendSelectionToNotes(selectionData) {
    if (!selectionData || !selectionData.text) return;
    try {
        const snippet = formatSelectionToMarkdown(selectionData);
        if (!snippet) return;

        const textarea = typeof document !== 'undefined' ? document.getElementById('NotesTextArea') : null;

        let currentNotes = '';
        if (typeof window !== 'undefined' && window.miseAPI && typeof window.miseAPI.readNotes === 'function') {
            try {
                currentNotes = (await window.miseAPI.readNotes()) || '';
            } catch (e) {
                currentNotes = textarea ? textarea.value : '';
            }
        } else if (textarea) {
            currentNotes = textarea.value;
        }

        const newContent = currentNotes.trim()
            ? `${currentNotes.trim()}${snippet}`
            : snippet.trim();

        if (textarea) {
            textarea.value = newContent;
            renderNotesView();
        }

        if (typeof window !== 'undefined' && window.miseAPI && typeof window.miseAPI.saveNotes === 'function') {
            await window.miseAPI.saveNotes(newContent);
        }

        const preview = selectionData.text.trim();
        const shortPreview = preview.length > 35 ? `${preview.slice(0, 32)}...` : preview;
        if (typeof window !== 'undefined' && typeof window.setTargetUrl === 'function') {
            window.setTargetUrl(`Saved "${shortPreview}" to Quick Notes (~/.config/mise-browser/notes.md)`, 3500);
        }
    } catch (err) {
        console.error('Failed to append selection to notes:', err);
    }
}

export function handleNotesGlobalKeyDown(e) {
    if (!state.notesActive) return;

    if (e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        e.stopImmediatePropagation();
        toggleNotesOverlay();
        return;
    }

    if (!state.isEditingNotes) {
        // Preview mode shortcuts
        if (e.key === 'e' || e.key === 'i' || e.key === 'Enter') {
            e.preventDefault();
            e.stopPropagation();
            state.isEditingNotes = true;
            renderNotesView();
            return;
        }

        if (e.key === 'f') {
            e.preventDefault();
            e.stopPropagation();
            toggleExpandNotesOverlay();
            return;
        }

        const mdView = typeof document !== 'undefined' ? document.getElementById('NotesMarkdownView') : null;
        if (mdView) {
            if (e.key === 'j' || e.key === 'ArrowDown') {
                e.preventDefault();
                mdView.scrollBy({ top: 60, behavior: 'smooth' });
                return;
            }
            if (e.key === 'k' || e.key === 'ArrowUp') {
                e.preventDefault();
                mdView.scrollBy({ top: -60, behavior: 'smooth' });
                return;
            }
            if (e.key === 'PageDown' || e.key === ' ') {
                e.preventDefault();
                mdView.scrollBy({ top: 320, behavior: 'smooth' });
                return;
            }
            if (e.key === 'PageUp') {
                e.preventDefault();
                mdView.scrollBy({ top: -320, behavior: 'smooth' });
                return;
            }
            if (e.key === 'Home') {
                e.preventDefault();
                mdView.scrollTo({ top: 0, behavior: 'smooth' });
                return;
            }
            if (e.key === 'End') {
                e.preventDefault();
                mdView.scrollTo({ top: mdView.scrollHeight, behavior: 'smooth' });
                return;
            }
        }
    } else {
        // Editing mode shortcuts
        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'e') {
            e.preventDefault();
            e.stopPropagation();
            state.isEditingNotes = false;
            renderNotesView();
            return;
        }

        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
            e.preventDefault();
            e.stopPropagation();
            const textarea = typeof document !== 'undefined' ? document.getElementById('NotesTextArea') : null;
            if (textarea && typeof window !== 'undefined' && window.miseAPI && typeof window.miseAPI.saveNotes === 'function') {
                window.miseAPI.saveNotes(textarea.value);
                if (typeof window.setTargetUrl === 'function') {
                    window.setTargetUrl('Quick Notes saved', 2000);
                }
            }
            return;
        }
    }
}

export function toggleNotesOverlay() {
    if (typeof document === 'undefined') {
        state.notesActive = !state.notesActive;
        return;
    }
    const overlay = document.getElementById('NotesOverlay');
    const textarea = document.getElementById('NotesTextArea');
    const mdView = document.getElementById('NotesMarkdownView');

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
            finishOpening(textarea.value);
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
    if (typeof document === 'undefined' || typeof window === 'undefined') return;
    const overlay = document.getElementById('NotesOverlay');
    const textarea = document.getElementById('NotesTextArea');
    const mdView = document.getElementById('NotesMarkdownView');
    const closeBtn = document.getElementById('CloseNotesBtn');
    const editBtn = document.getElementById('ToggleEditNotesBtn');
    const expandBtn = document.getElementById('ToggleExpandNotesBtn');
    const externalBtn = document.getElementById('OpenExternalNotesBtn');

    window.addEventListener('keydown', handleNotesGlobalKeyDown, true);

    if (overlay) {
        overlay.addEventListener('click', (e) => {
            if (e.target === overlay) {
                toggleNotesOverlay();
            }
        });
    }

    if (editBtn) {
        editBtn.addEventListener('click', () => {
            state.isEditingNotes = !state.isEditingNotes;
            renderNotesView();
        });
    }

    if (expandBtn) {
        expandBtn.addEventListener('click', toggleExpandNotesOverlay);
    }

    if (externalBtn) {
        externalBtn.addEventListener('click', openNotesInExternalApp);
    }

    if (mdView) {
        mdView.addEventListener('click', (e) => {
            const link = e.target.closest('a');
            if (link && link.href) {
                e.preventDefault();
                spawnTabWithUrl(link.href);
                toggleNotesOverlay();
            } else {
                mdView.focus();
            }
        });
    }

    if (textarea) {
        textarea.addEventListener('input', (e) => {
            notesLoaded = true;
            notesEdited = true;
            const content = e.target.value;
            updateWordCountBadge(e.target.value);
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
