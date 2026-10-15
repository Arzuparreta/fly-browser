import { prepareOverlay } from '../focus.js';
import { state } from '../state.js';
import { escapeHtml, focusActiveWebview, getActiveWebview } from '../utils.js';
import { renderWorkspaceUI, switchTabFocus, spawnTabWithUrl, spawnNewBlankTab, handleTabRemoval } from '../webview.js';
import { dismissAddressBar } from '../navigation.js';

export async function loadBookmarksAndQuickmarks() {
    if (window.flyAPI) {
        state.bookmarks = (await window.flyAPI.readBookmarks()) || [];
        state.quickmarks = (await window.flyAPI.readQuickmarks()) || {};
    }
}

export function promptQuickmark(mode) {
    state.quickmarkMode = mode;
    state.awaitingQuickmarkKey = true;
    // The prompt ends by focusing the page from the body, which raises no focusout, so an
    // open address bar would stay on screen without focus. Other surfaces stay as they were.
    dismissAddressBar();

    if (document.activeElement) {
        document.activeElement.blur();
    }
    window.focus();

    const listener = (e) => {
        if (['control', 'shift', 'alt', 'meta'].includes(e.key.toLowerCase())) {
            return;
        }

        e.preventDefault();
        e.stopPropagation();
        window.removeEventListener('keydown', listener, true);

        state.awaitingQuickmarkKey = false;

        if (e.key === 'Escape') {
            focusActiveWebview();
            return;
        }

        const key = e.key.toLowerCase();

        if (state.quickmarkMode === 'set') {
            const activeWv = getActiveWebview();
            if (!activeWv) return;
            const url = activeWv.getURL();
            const title = activeWv.getTitle() || url;

            state.quickmarks[key] = { url, title };
            if (window.flyAPI && typeof window.flyAPI.saveQuickmarks === 'function') {
                window.flyAPI.saveQuickmarks(state.quickmarks);
            }
            focusActiveWebview();
        } else if (state.quickmarkMode === 'jump') {
            if (state.quickmarks[key]) {
                spawnTabWithUrl(state.quickmarks[key].url);
            } else {
                focusActiveWebview();
            }
        }
    };

    window.addEventListener('keydown', listener, true);
}

export async function addCurrentPageToBookmarks() {
    const activeWv = getActiveWebview();
    if (!activeWv) return;
    
    const url = activeWv.getURL();
    const title = activeWv.getTitle() || url;
    
    if (!url || url === 'about:blank') return;

    if (!state.bookmarks.some(b => b.url === url)) {
        state.bookmarks.unshift({ title, url, timestamp: Date.now() });
        await window.flyAPI.saveBookmarks(state.bookmarks);
    }
}

export async function toggleBookmarksOverlay() {
    const overlay = document.getElementById('BookmarksOverlay');
    const input = document.getElementById('BookmarkSearchInput');

    if (!overlay) return;

    state.bookmarksActive = !state.bookmarksActive;

    if (state.bookmarksActive) {
        prepareOverlay('BookmarksOverlay');
        // Hide webviews to ensure guest process releases keyboard focus cleanly
        const container = document.getElementById('webview-container');
        if (container) {
            const allWebviews = container.querySelectorAll('webview');
            allWebviews.forEach((wv) => wv.style.display = 'none');
        }

        overlay.style.display = 'flex';

        // Initialise column and selection indices
        const hasQuickmarks = state.quickmarks && Object.keys(state.quickmarks).length > 0;
        state.bookmarkActiveColumn = hasQuickmarks ? 'quickmarks' : 'bookmarks';
        state.quickmarkSelectionIdx = 0;
        state.bookmarkSelectionIdx = 0;

        renderBookmarksList('');

        // Focus search input immediately so keyboard input is captured without delay
        if (input) {
            input.value = '';
            input.focus();
        } else {
            overlay.focus();
        }

        // Sync fresh data from disk in background without blocking initial focus
        loadBookmarksAndQuickmarks().then(() => {
            if (state.bookmarksActive) {
                renderBookmarksList(input ? input.value : '');
            }
        }).catch(() => {});
    } else {
        overlay.style.display = 'none';
        const activeListItem = document.querySelector('#TabList li.selected');
        const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;
        switchTabFocus(currentIdx);
    }
}

export function renderBookmarksList(filterText = '') {
    const qmContainer = document.getElementById('QuickmarksResultsList');
    const bmContainer = document.getElementById('BookmarksResultsList');
    if (!qmContainer || !bmContainer) return;

    qmContainer.innerHTML = '';
    bmContainer.innerHTML = '';
    const query = filterText.toLowerCase().trim();
    state.filteredQuickmarksCache = [];
    state.filteredBookmarksCache = [];

    // Build quickmarks cache and dom
    const quickmarkKeys = Object.keys(state.quickmarks).filter(key => {
        const qm = state.quickmarks[key];
        return key.toLowerCase().includes(query) || 
               (qm.title && qm.title.toLowerCase().includes(query)) || 
               (qm.url && qm.url.toLowerCase().includes(query));
    });

    if (quickmarkKeys.length > 0) {
        quickmarkKeys.forEach(key => {
            const qm = state.quickmarks[key];
            const itemObj = { type: 'quickmark', key: key, url: qm.url, title: qm.title };
            state.filteredQuickmarksCache.push(itemObj);

            const itemEl = document.createElement('div');
            itemEl.className = 'bookmark-item-row quickmark-row';
            itemEl.setAttribute('tabindex', '-1');

            itemEl.innerHTML = `
                <div class="quickmark-key-badge">${escapeHtml(key.toUpperCase())}</div>
                <div class="bookmark-info">
                    <div class="history-item-title">${escapeHtml(qm.title)}</div>
                    <div class="history-item-url">${escapeHtml(qm.url)}</div>
                </div>
                <button class="bookmark-delete-btn" tabindex="-1" title="Delete Quickmark"><i class="fa-solid fa-trash-can"></i></button>
            `;

            itemEl.addEventListener('mouseenter', () => {
                state.bookmarkActiveColumn = 'quickmarks';
                state.quickmarkSelectionIdx = state.filteredQuickmarksCache.indexOf(itemObj);
                updateBookmarkVisualSelection();
            });

            itemEl.addEventListener('click', (e) => {
                if (e.target.closest('.bookmark-delete-btn')) return;
                spawnTabWithUrl(qm.url);
                if (state.bookmarksActive) {
                    toggleBookmarksOverlay();
                }
            });

            itemEl.querySelector('.bookmark-delete-btn').addEventListener('click', async (e) => {
                e.stopPropagation();
                deleteQuickmark(key);
            });

            qmContainer.appendChild(itemEl);
        });
    } else {
        qmContainer.innerHTML = '<div class="history-empty">No quickmarks found.</div>';
    }

    // Build standard bookmarks cache and dom
    const matchingBookmarks = state.bookmarks.filter(bm => 
        (bm.title && bm.title.toLowerCase().includes(query)) || 
        (bm.url && bm.url.toLowerCase().includes(query))
    );

    if (matchingBookmarks.length > 0) {
        matchingBookmarks.forEach(bm => {
            const itemObj = { type: 'bookmark', url: bm.url, title: bm.title };
            state.filteredBookmarksCache.push(itemObj);

            const itemEl = document.createElement('div');
            itemEl.className = 'bookmark-item-row';
            itemEl.setAttribute('tabindex', '-1');

            itemEl.innerHTML = `
                <div class="bookmark-info">
                    <div class="history-item-title">${escapeHtml(bm.title)}</div>
                    <div class="history-item-url">${escapeHtml(bm.url)}</div>
                </div>
                <button class="bookmark-delete-btn" tabindex="-1" title="Delete Bookmark"><i class="fa-solid fa-trash-can"></i></button>
            `;

            itemEl.addEventListener('mouseenter', () => {
                state.bookmarkActiveColumn = 'bookmarks';
                state.bookmarkSelectionIdx = state.filteredBookmarksCache.indexOf(itemObj);
                updateBookmarkVisualSelection();
            });

            itemEl.addEventListener('click', (e) => {
                if (e.target.closest('.bookmark-delete-btn')) return;
                spawnTabWithUrl(bm.url);
                if (state.bookmarksActive) {
                    toggleBookmarksOverlay();
                }
            });

            itemEl.querySelector('.bookmark-delete-btn').addEventListener('click', async (e) => {
                e.stopPropagation();
                deleteBookmark(bm.url);
            });

            bmContainer.appendChild(itemEl);
        });
    } else {
        bmContainer.innerHTML = '<div class="history-empty">No bookmarks found.</div>';
    }

    if (state.quickmarkSelectionIdx >= state.filteredQuickmarksCache.length) {
        state.quickmarkSelectionIdx = Math.max(0, state.filteredQuickmarksCache.length - 1);
    }
    if (state.bookmarkSelectionIdx >= state.filteredBookmarksCache.length) {
        state.bookmarkSelectionIdx = Math.max(0, state.filteredBookmarksCache.length - 1);
    }

    // Auto-select the populated column if current column has no matching results
    if (state.bookmarkActiveColumn === 'quickmarks' && state.filteredQuickmarksCache.length === 0 && state.filteredBookmarksCache.length > 0) {
        state.bookmarkActiveColumn = 'bookmarks';
    } else if (state.bookmarkActiveColumn === 'bookmarks' && state.filteredBookmarksCache.length === 0 && state.filteredQuickmarksCache.length > 0) {
        state.bookmarkActiveColumn = 'quickmarks';
    }

    updateBookmarkVisualSelection();
}

export function updateBookmarkVisualSelection() {
    const qmItems = document.querySelectorAll('#QuickmarksResultsList .bookmark-item-row');
    const bmItems = document.querySelectorAll('#BookmarksResultsList .bookmark-item-row');
    const qmPane = document.getElementById('QuickmarksResultsList')?.closest('.bookmarks-column-pane');
    const bmPane = document.getElementById('BookmarksResultsList')?.closest('.bookmarks-column-pane');

    qmItems.forEach((item, idx) => {
        if (state.bookmarkActiveColumn === 'quickmarks' && idx === state.quickmarkSelectionIdx) {
            item.classList.add('selected');
            item.scrollIntoView({ block: 'nearest' });
        } else {
            item.classList.remove('selected');
        }
    });

    bmItems.forEach((item, idx) => {
        if (state.bookmarkActiveColumn === 'bookmarks' && idx === state.bookmarkSelectionIdx) {
            item.classList.add('selected');
            item.scrollIntoView({ block: 'nearest' });
        } else {
            item.classList.remove('selected');
        }
    });

    if (qmPane && bmPane) {
        if (state.bookmarkActiveColumn === 'quickmarks') {
            qmPane.classList.add('active-pane');
            bmPane.classList.remove('active-pane');
        } else {
            bmPane.classList.add('active-pane');
            qmPane.classList.remove('active-pane');
        }
    }
}

export async function deleteBookmark(url) {
    if (!url) return;
    state.bookmarks = state.bookmarks.filter(bm => bm.url !== url);
    if (window.flyAPI && typeof window.flyAPI.saveBookmarks === 'function') {
        await window.flyAPI.saveBookmarks(state.bookmarks);
    }
    const searchInput = document.getElementById('BookmarkSearchInput');
    renderBookmarksList(searchInput ? searchInput.value : '');
}

export async function deleteQuickmark(key) {
    if (!key || !state.quickmarks[key]) return;
    delete state.quickmarks[key];
    if (window.flyAPI && typeof window.flyAPI.saveQuickmarks === 'function') {
        window.flyAPI.saveQuickmarks(state.quickmarks);
    }
    const searchInput = document.getElementById('BookmarkSearchInput');
    renderBookmarksList(searchInput ? searchInput.value : '');
}

export function handleBookmarkKeyNavigation(e) {
    if (!state.bookmarksActive) return;

    const searchInput = document.getElementById('BookmarkSearchInput');
    const isSearchFocused = document.activeElement === searchInput;

    if (e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        toggleBookmarksOverlay();
        return;
    }

    // Column switching with ArrowLeft / ArrowRight
    if (e.key === 'ArrowRight') {
        if (!isSearchFocused || (searchInput && searchInput.selectionStart === searchInput.value.length)) {
            e.preventDefault();
            e.stopPropagation();
            state.bookmarkActiveColumn = 'bookmarks';
            updateBookmarkVisualSelection();
            return;
        }
    } else if (e.key === 'ArrowLeft') {
        if (!isSearchFocused || (searchInput && searchInput.selectionStart === 0)) {
            e.preventDefault();
            e.stopPropagation();
            state.bookmarkActiveColumn = 'quickmarks';
            updateBookmarkVisualSelection();
            return;
        }
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        e.stopPropagation();
        let currentCache = state.bookmarkActiveColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
        if (currentCache.length === 0) {
            const altColumn = state.bookmarkActiveColumn === 'quickmarks' ? 'bookmarks' : 'quickmarks';
            const altCache = altColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
            if (altCache.length > 0) {
                state.bookmarkActiveColumn = altColumn;
                currentCache = altCache;
            }
        }
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkActiveColumn === 'quickmarks' ? state.quickmarkSelectionIdx : state.bookmarkSelectionIdx;
            if (e.key === 'ArrowDown') {
                selIdx = (selIdx + 1) % currentCache.length;
            } else {
                selIdx = (selIdx - 1 + currentCache.length) % currentCache.length;
            }
            if (state.bookmarkActiveColumn === 'quickmarks') state.quickmarkSelectionIdx = selIdx;
            else state.bookmarkSelectionIdx = selIdx;
            updateBookmarkVisualSelection();
        }
        return;
    } else if (e.key === 'PageDown' || e.key === 'PageUp') {
        e.preventDefault();
        e.stopPropagation();
        let currentCache = state.bookmarkActiveColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
        if (currentCache.length === 0) {
            const altColumn = state.bookmarkActiveColumn === 'quickmarks' ? 'bookmarks' : 'quickmarks';
            const altCache = altColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
            if (altCache.length > 0) {
                state.bookmarkActiveColumn = altColumn;
                currentCache = altCache;
            }
        }
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkActiveColumn === 'quickmarks' ? state.quickmarkSelectionIdx : state.bookmarkSelectionIdx;
            if (e.key === 'PageDown') {
                selIdx = Math.min(currentCache.length - 1, selIdx + 5);
            } else {
                selIdx = Math.max(0, selIdx - 5);
            }
            if (state.bookmarkActiveColumn === 'quickmarks') state.quickmarkSelectionIdx = selIdx;
            else state.bookmarkSelectionIdx = selIdx;
            updateBookmarkVisualSelection();
        }
        return;
    } else if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        let currentCache = state.bookmarkActiveColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
        if (currentCache.length === 0) {
            const altColumn = state.bookmarkActiveColumn === 'quickmarks' ? 'bookmarks' : 'quickmarks';
            const altCache = altColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
            if (altCache.length > 0) {
                state.bookmarkActiveColumn = altColumn;
                currentCache = altCache;
            }
        }
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkActiveColumn === 'quickmarks' ? state.quickmarkSelectionIdx : state.bookmarkSelectionIdx;
            const targetItem = currentCache[selIdx];
            if (targetItem && targetItem.url) {
                spawnTabWithUrl(targetItem.url);
                if (state.bookmarksActive) {
                    toggleBookmarksOverlay();
                }
            }
        }
        return;
    } else if (e.key === 'Delete') {
        if (isSearchFocused && searchInput && searchInput.selectionStart !== searchInput.selectionEnd) {
            return;
        }
        if (isSearchFocused && searchInput && searchInput.value.length > 0 && searchInput.selectionStart < searchInput.value.length) {
            return;
        }
        e.preventDefault();
        e.stopPropagation();
        const currentCache = state.bookmarkActiveColumn === 'quickmarks' ? state.filteredQuickmarksCache : state.filteredBookmarksCache;
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkActiveColumn === 'quickmarks' ? state.quickmarkSelectionIdx : state.bookmarkSelectionIdx;
            const targetItem = currentCache[selIdx];
            if (targetItem) {
                if (targetItem.type === 'quickmark') {
                    deleteQuickmark(targetItem.key);
                } else if (targetItem.type === 'bookmark') {
                    deleteBookmark(targetItem.url);
                }
            }
        }
        return;
    }
}

let bookmarkListenersInitialised = false;

export function setupBookmarkOverlayListeners() {
    if (bookmarkListenersInitialised) return;

    const searchInput = document.getElementById('BookmarkSearchInput');
    const closeBtn = document.getElementById('CloseBookmarksBtn');
    const overlay = document.getElementById('BookmarksOverlay');
    const qmList = document.getElementById('QuickmarksResultsList');
    const bmList = document.getElementById('BookmarksResultsList');

    if (!overlay && !searchInput) return;
    bookmarkListenersInitialised = true;

    if (searchInput) {
        searchInput.setAttribute('tabindex', '0');
        searchInput.addEventListener('input', (e) => {
            state.quickmarkSelectionIdx = 0;
            state.bookmarkSelectionIdx = 0;
            renderBookmarksList(e.target.value);
        });
        searchInput.addEventListener('keydown', handleBookmarkKeyNavigation);
    }

    if (closeBtn) {
        closeBtn.setAttribute('tabindex', '-1');
        closeBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            toggleBookmarksOverlay();
        });
    }

    if (overlay) {
        overlay.setAttribute('tabindex', '-1');
        overlay.addEventListener('keydown', handleBookmarkKeyNavigation);
        overlay.addEventListener('click', (e) => {
            if (e.target.id === 'BookmarksOverlay' || e.target.classList.contains('bookmarks-columns-container')) {
                toggleBookmarksOverlay();
            }
        });
    }

    if (qmList) {
        qmList.setAttribute('tabindex', '-1');
        qmList.closest('.bookmarks-column-pane')?.addEventListener('click', () => {
            if (state.bookmarkActiveColumn !== 'quickmarks') {
                state.bookmarkActiveColumn = 'quickmarks';
                updateBookmarkVisualSelection();
            }
            if (searchInput) searchInput.focus();
        });
    }

    if (bmList) {
        bmList.setAttribute('tabindex', '-1');
        bmList.closest('.bookmarks-column-pane')?.addEventListener('click', () => {
            if (state.bookmarkActiveColumn !== 'bookmarks') {
                state.bookmarkActiveColumn = 'bookmarks';
                updateBookmarkVisualSelection();
            }
            if (searchInput) searchInput.focus();
        });
    }

    window.addEventListener('keydown', handleBookmarkKeyNavigation, true);
}

if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', setupBookmarkOverlayListeners);
    } else {
        setupBookmarkOverlayListeners();
    }
}

