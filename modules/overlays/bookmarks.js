import { prepareOverlay } from '../focus.js';
import { state } from '../state.js';
import { escapeHtml, getActiveWebview } from '../utils.js';
import { switchTabFocus, spawnTabWithUrl } from '../webview.js';

export async function loadBookmarks() {
    if (window.flyAPI) {
        state.bookmarks = (await window.flyAPI.readBookmarks()) || [];
    }
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

        // Initialise the list selection.
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
        loadBookmarks().then(() => {
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
    const bmContainer = document.getElementById('BookmarksResultsList');
    if (!bmContainer) return;

    bmContainer.innerHTML = '';
    const query = filterText.toLowerCase().trim();
    state.filteredBookmarksCache = [];

    // Build standard bookmarks cache and dom
    const matchingBookmarks = state.bookmarks.filter(bm => 
        (bm.title && bm.title.toLowerCase().includes(query)) || 
        (bm.url && bm.url.toLowerCase().includes(query))
    );

    if (matchingBookmarks.length > 0) {
        matchingBookmarks.forEach(bm => {
            const itemObj = { url: bm.url, title: bm.title };
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

    if (state.bookmarkSelectionIdx >= state.filteredBookmarksCache.length) {
        state.bookmarkSelectionIdx = Math.max(0, state.filteredBookmarksCache.length - 1);
    }

    updateBookmarkVisualSelection();
}

export function updateBookmarkVisualSelection() {
    document.querySelectorAll('#BookmarksResultsList .bookmark-item-row').forEach((item, index) => {
        const selected = index === state.bookmarkSelectionIdx;
        item.classList.toggle('selected', selected);
        if (selected) item.scrollIntoView({block: 'nearest'});
    });
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

    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        e.stopPropagation();
        const currentCache = state.filteredBookmarksCache;
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkSelectionIdx;
            if (e.key === 'ArrowDown') {
                selIdx = (selIdx + 1) % currentCache.length;
            } else {
                selIdx = (selIdx - 1 + currentCache.length) % currentCache.length;
            }
            state.bookmarkSelectionIdx = selIdx;
            updateBookmarkVisualSelection();
        }
        return;
    } else if (e.key === 'PageDown' || e.key === 'PageUp') {
        e.preventDefault();
        e.stopPropagation();
        const currentCache = state.filteredBookmarksCache;
        if (currentCache.length > 0) {
            let selIdx = state.bookmarkSelectionIdx;
            if (e.key === 'PageDown') {
                selIdx = Math.min(currentCache.length - 1, selIdx + 5);
            } else {
                selIdx = Math.max(0, selIdx - 5);
            }
            state.bookmarkSelectionIdx = selIdx;
            updateBookmarkVisualSelection();
        }
        return;
    } else if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        const currentCache = state.filteredBookmarksCache;
        if (currentCache.length > 0) {
            const selIdx = state.bookmarkSelectionIdx;
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
        const currentCache = state.filteredBookmarksCache;
        if (currentCache.length > 0) {
            const selIdx = state.bookmarkSelectionIdx;
            const targetItem = currentCache[selIdx];
            if (targetItem) deleteBookmark(targetItem.url);
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
    const bmList = document.getElementById('BookmarksResultsList');

    if (!overlay && !searchInput) return;
    bookmarkListenersInitialised = true;

    if (searchInput) {
        searchInput.setAttribute('tabindex', '0');
        searchInput.addEventListener('input', (e) => {
            state.bookmarkSelectionIdx = 0;
            renderBookmarksList(e.target.value);
        });
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
        overlay.addEventListener('click', (e) => {
            if (e.target.id === 'BookmarksOverlay' || e.target.classList.contains('bookmarks-list-container')) {
                toggleBookmarksOverlay();
            }
        });
    }

    if (bmList) {
        bmList.setAttribute('tabindex', '-1');
        bmList.closest('.bookmarks-list-pane')?.addEventListener('click', () => searchInput?.focus());
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

