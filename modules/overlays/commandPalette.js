import { prepareOverlay } from '../focus.js';
import { initialiseKeyboardEditor, showKeyboardEditor, hideKeyboardEditor } from '../keyboardSettings.js';
import { initialiseSettingsNav, showSettingsPane, currentSettingsPane, clearSettingsSearch } from '../settingsNav.js';
import { state } from '../state.js';
import { focusActiveWebview, getActiveWebview, isTargetScript, isDarkMode, escapeHtml } from '../utils.js';
import { renderWorkspaceUI, switchTabFocus, spawnTabWithUrl, spawnNewBlankTab, handleTabRemoval, hibernateInactiveTabs, wakeAllTabsInWorkspace } from '../webview.js';
import { toggleBookmarksOverlay, promptQuickmark, addCurrentPageToBookmarks } from './bookmarks.js';
import { syncVisualSettingsInputs, setupVisualSettingsListeners } from '../theme.js';
import { getExtensionPaletteCommands, prepareExtensionPaletteCommands, syncExtensionsPreferences, setupExtensionsPreferencesListeners } from '../extensions.js';
import { renderSearchAliasesUI } from './searchEngine.js';

function returnFocusToWebview() {
    window.miseAllowWebviewFocus = true;
    focusActiveWebview();
}

export const COMMAND_DEFINITIONS = [
    // Tabs & Navigation
    {
        id: 'spawn-tab',
        title: 'New Blank Tab',
        desc: 'Open a fresh tab using the configured search engine',
        details: 'Creates an isolated Electron webview instance inside the current workspace, initialising with the default search engine whilst respecting your memory configuration.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-plus',
        actionId: 'spawn-tab',
        keywords: ['tab', 'open', 'new', 'create', 'blank'],
        action: () => spawnNewBlankTab()
    },
    {
        id: 'toggle-address',
        title: 'Toggle Floating Address Bar',
        desc: 'Open floating address bar for URL entry and search engine aliases',
        details: 'Displays the modal address bar supporting direct URL routing, query auto-completion, and bang search engine aliases such as !w for Wikipedia.',
        tip: 'Type ! to browse active search aliases or paste raw URLs to navigate instantly.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-compass',
        actionId: 'toggle-address',
        keywords: ['url', 'address', 'search', 'goto', 'navigate', 'location'],
        action: () => window.displayAddressOverlay && window.displayAddressOverlay()
    },
    {
        id: 'toggle-dashboard',
        title: 'Toggle Workspace Dashboard',
        desc: 'Full-page tree view of all workspaces, tabs, and windows',
        details: 'Renders an expansive visual hierarchy of every workspace, active tab, and sleeping webview across the session with bulk management controls.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-layer-group',
        actionId: 'toggle-dashboard',
        keywords: ['workspace', 'tree', 'tabs', 'all', 'overview', 'dashboard'],
        action: () => window.toggleDashboardView && window.toggleDashboardView()
    },
    {
        id: 'toggle-history',
        title: 'Toggle Actionable History',
        desc: 'Search, filter by domain, or purge browsing history records',
        details: 'Opens the searchable history ledger stored locally, featuring domain-level filtering and surgical record purging.',
        tip: 'Filter by specific domains to audit past navigation or clear privacy-sensitive sessions.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-clock-rotate-left',
        actionId: 'toggle-history',
        keywords: ['history', 'recent', 'visited', 'log', 'purge'],
        action: () => window.toggleHistoryOverlay && window.toggleHistoryOverlay()
    },
    {
        id: 'toggle-status-bar',
        title: 'Toggle Status & Mode Bar',
        desc: 'Show or hide the bottom status bar displaying mode, target URL, and TLS state',
        details: 'Toggles the 22px footer status bar across the bottom of the content area. Displays real-time link hover targets, TLS encryption state, active container partition, and modal state.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-window-minimize',
        actionId: 'toggle-status-bar',
        keywords: ['status', 'mode', 'bar', 'footer', 'url preview', 'toggle', 'bottom'],
        action: () => window.toggleStatusBar && window.toggleStatusBar()
    },
    {
        id: 'toggle-clock',
        title: 'Toggle Clock & Date Display',
        desc: 'Show or hide the live clock in the status bar and sidebar',
        details: 'Toggles the real-time clock widget displaying local date and time in the bottom status bar and vertical sidebar.',
        tip: 'Click on the clock at any time to cycle between date and time, time only, and seconds.',
        category: 'Preferences & System',
        icon: 'fa-regular fa-clock',
        keywords: ['clock', 'time', 'date', 'watch', 'hours', 'minutes', 'status'],
        action: () => window.toggleClock && window.toggleClock()
    },
    {
        id: 'cycle-clock-format',
        title: 'Cycle Clock Format (Date & Time / Time / Seconds)',
        desc: 'Switch between date and time, compact time, or time with seconds',
        details: 'Rotates the clock presentation mode between standard UK date and time, compact time-only, and high-precision seconds.',
        tip: 'You can also click the clock widget directly in the status bar or sidebar.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-stopwatch',
        keywords: ['clock', 'format', 'seconds', 'time', 'date', 'cycle', 'switch'],
        action: () => window.cycleClockFormat && window.cycleClockFormat()
    },
    {
        id: 'toggle-window-controls',
        title: 'Toggle Window Controls (Minimise, Maximise, Close)',
        desc: 'Show or hide window action buttons in the sidebar for desktop environments',
        details: 'Displays window management buttons at the top of the vertical sidebar. Ideal for desktop environments such as KDE or GNOME where native window buttons are hidden or disabled.',
        tip: 'Enable this if your desktop environment hides title bars on maximised windows.',
        category: 'Preferences & System',
        icon: 'fa-regular fa-window-restore',
        keywords: ['window', 'controls', 'minimize', 'minimise', 'maximize', 'maximise', 'close', 'restore', 'buttons', 'kde', 'gnome'],
        action: () => window.toggleWindowControls && window.toggleWindowControls()
    },
    {
        id: 'trigger-hints',
        title: 'Toggle Link Hints Overlay',
        desc: 'Display letter tags over clickable links for mouse-free browsing',
        details: 'Injects minimal letter tags over all actionable elements, hyperlinks, and inputs in the viewport to enable rapid keyboard-only navigation.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-keyboard',
        actionId: 'trigger-hints',
        keywords: ['hints', 'click', 'links', 'letters', 'vim', 'keyboard'],
        action: () => window.triggerLinkHints && window.triggerLinkHints()
    },
    {
        id: 'toggle-find',
        title: 'Find In Page',
        desc: 'Search for text matches within the current active webpage',
        details: 'Invokes the in-page search engine across the active webview frame, highlighting all occurrences with instant keyboard cycling.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-magnifying-glass',
        actionId: 'toggle-find',
        keywords: ['find', 'search', 'page', 'text', 'lookup'],
        action: () => window.toggleInPageSearch && window.toggleInPageSearch()
    },
    {
        id: 'reload-active-tab',
        title: 'Reload Active Tab',
        desc: 'Refresh the current webpage document and user assets',
        details: 'Forces the active webview renderer to re-fetch the current URL whilst preserving your workspace tab state and session cache.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-arrows-rotate',
        actionId: 'reload-active-tab',
        keywords: ['reload', 'refresh', 'page', 'update'],
        action: () => {
            const wv = getActiveWebview();
            if (wv) wv.reload();
        }
    },
    {
        id: 'remove-tab',
        title: 'Close Current Tab',
        desc: 'Tear down active webview and focus the adjacent tab',
        details: 'Safely disposes of the active webview frame, releasing allocated V8 heap memory and re-focusing the nearest remaining tab.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-xmark',
        actionId: 'remove-tab',
        keywords: ['close', 'remove', 'delete', 'tab', 'exit'],
        action: () => handleTabRemoval()
    },
    {
        id: 'toggle-pin-tab',
        title: 'Pin / Unpin Active Tab',
        desc: 'Pin essential tab to the top of workspace or unpin it',
        details: 'Pinned tabs stay anchored at the top of the tab list, are protected from accidental closure, and are exempted from automatic memory hibernation.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-thumbtack',
        actionId: 'toggle-pin-tab',
        keywords: ['pin', 'unpin', 'sticky', 'essential', 'lock', 'tab'],
        action: () => window.togglePinActiveTab && window.togglePinActiveTab()
    },
    {
        id: 'reset-zoom',
        title: 'Reset Tab Zoom Level',
        desc: 'Restore webpage zoom magnification back to 100% default',
        details: 'Resets the webview viewport zoom factor to unity (0 level), clearing custom page scalings applied via trackpad or shortcuts.',
        tip: 'Quickly recover standard layout proportions if a page was accidentally zoomed.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-magnifying-glass-arrows-rotate',
        keywords: ['zoom', 'scale', 'magnify', 'size', '100%'],
        action: () => {
            const currentWS = state.sessionState.current_workspace;
            const activeListItem = document.querySelector('#TabList li.selected');
            if (!activeListItem) return;
            const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
            const activeWv = state.activeViewsCache[currentWS]?.[currentIdx];
            if (activeWv) activeWv.setZoomLevel(0);
        }
    },
    {
        id: 'focus-sidebar',
        title: 'Toggle Sidebar Focus',
        desc: 'Move focus between the sidebar and the active page',
        details: 'Opens and focuses the tab list. Run again from any sidebar control to return to the page and collapse an unpinned sidebar. A pinned sidebar stays open.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-bars',
        actionId: 'focus-sidebar',
        keywords: ['sidebar', 'tabs', 'focus', 'list'],
        action: () => {
            const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
            if (selectedTab) selectedTab.focus();
        }
    },
    {
        id: 'focus-nav-buttons',
        title: 'Focus Navigation Buttons',
        desc: 'Cycle focus across Back, Forward, Sidebar, Split, and Menu buttons',
        details: 'Places keyboard focus onto the primary toolbar buttons, allowing navigation using left and right arrow keys.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-arrow-right-arrow-left',
        actionId: 'focus-nav-buttons',
        keywords: ['buttons', 'back', 'forward', 'top', 'controls'],
        action: () => {
            const btn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
            if (btn) btn.focus();
        }
    },
    {
        id: 'focus-webview',
        title: 'Focus Active Webview',
        desc: 'Return keyboard and input focus to the active webpage',
        details: 'Transfers OS input focus directly back into the guest webpage document, allowing uninterrupted typing and interaction.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-window-restore',
        actionId: 'focus-webview',
        keywords: ['focus', 'webview', 'page', 'content'],
        action: () => returnFocusToWebview()
    },
    {
        id: 'toggle-sidebar-pin',
        actionId: 'toggle-zen-mode',
        title: 'Toggle Sidebar Expansion (Pin / Unpin)',
        desc: 'Pin sidebar open or auto-shrink to 36px strip expanding on hover',
        details: 'Configures auto-collapse behaviour: when unpinned, the sidebar shrinks to 36px and slides out smoothly when your cursor hovers nearby.',
        tip: 'Unpin for an immersive browsing canvas that leaves controls within easy reach.',
        category: 'Tabs & Navigation',
        icon: 'fa-solid fa-thumbtack',
        keywords: ['pin', 'lock', 'unpin', 'autohide', 'strip'],
        action: () => window.toggleSidebarPin && window.toggleSidebarPin()
    },
    // Split View
    {
        id: 'toggle-split-vertical',
        title: 'Toggle Dual-Split View (Side-by-Side)',
        desc: 'Tile two webviews side by side in equal columns for multitasking',
        details: 'Splits the active viewport into two concurrent columns, allowing side-by-side comparison, documentation reading, or multitasking.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Split View',
        icon: 'fa-solid fa-table-columns',
        actionId: 'toggle-split',
        keywords: ['split', 'dual', 'side', 'vertical', 'columns', 'multitask'],
        action: () => window.toggleSplitView && window.toggleSplitView('vertical')
    },
    {
        id: 'toggle-split-horizontal',
        title: 'Toggle Dual-Split View (Stacked)',
        desc: 'Tile two webviews stacked vertically in top and bottom rows',
        details: 'Divides the viewport horizontally into stacked top and bottom panes, ideal for viewing reference materials above a web console.',
        tip: 'Useful on vertical monitors or when referencing notes above an active page.',
        category: 'Split View',
        icon: 'fa-solid fa-table-rows',
        keywords: ['split', 'horizontal', 'stacked', 'rows', 'top', 'bottom'],
        action: () => window.toggleSplitView && window.toggleSplitView('horizontal')
    },
    {
        id: 'cycle-split-orientation',
        title: 'Cycle Split View Orientation',
        desc: 'Switch active dual-split layout between side-by-side and stacked',
        details: 'Toggles active multi-pane geometry between side-by-side columns and stacked rows without reloading either webview.',
        tip: 'Adapt your workspace layout on the fly depending on webpage content.',
        category: 'Split View',
        icon: 'fa-solid fa-arrows-rotate',
        keywords: ['split', 'rotate', 'cycle', 'layout', 'orientation'],
        action: () => window.cycleSplitOrientation && window.cycleSplitOrientation()
    },
    {
        id: 'switch-split-focus',
        title: 'Switch Split Pane Focus',
        desc: 'Move active keyboard and scroll focus to the opposite split pane',
        details: 'Switches the active input target between primary and secondary split panes without needing cursor clicks.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Split View',
        icon: 'fa-solid fa-arrows-split-up-and-left',
        actionId: 'switch-split-focus',
        keywords: ['split', 'focus', 'pane', 'switch', 'alternate'],
        action: () => window.switchSplitFocus && window.switchSplitFocus()
    },
    {
        id: 'swap-split-panes',
        title: 'Swap Split Panes',
        desc: 'Exchange positions of the primary and secondary split panes',
        details: 'Reverses the left/right or top/bottom layout ordering of the active dual panes whilst maintaining their states.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Split View',
        icon: 'fa-solid fa-right-left',
        actionId: 'swap-split-panes',
        keywords: ['split', 'swap', 'flip', 'reverse', 'exchange'],
        action: () => window.swapSplitPanes && window.swapSplitPanes()
    },
    {
        id: 'close-split-view',
        title: 'Close Split View',
        desc: 'Exit dual-split mode and return to standard single tab viewport',
        details: 'Closes the secondary split pane container and restores the primary webview to the full single viewport.',
        tip: 'Quickly return to single-task focus once your comparison work is finished.',
        category: 'Split View',
        icon: 'fa-solid fa-rectangle-xmark',
        keywords: ['split', 'close', 'exit', 'single'],
        action: () => window.closeSplitView && window.closeSplitView()
    },

    // Performance & Memory
    {
        id: 'hibernate-tabs',
        title: 'Hibernate Inactive Tabs',
        desc: 'Detach idle background tabs from DOM to free CPU and system RAM',
        details: 'Detaches background webview renderers from the active DOM tree, suspending JavaScript execution and releasing system RAM while saving tab URLs.',
        tip: 'Run this periodically during heavy research sessions to keep memory usage under 600 MB.',
        category: 'Performance & Memory',
        icon: 'fa-solid fa-bed',
        keywords: ['ram', 'memory', 'sleep', 'hibernate', 'clean', 'suspend', 'free'],
        action: () => hibernateInactiveTabs()
    },
    {
        id: 'wake-all-tabs',
        title: 'Wake All Tabs in Workspace',
        desc: 'Reload and restore all hibernating tabs in active workspace',
        details: 'Iterates through all sleeping tabs in the active workspace, reattaching them to the DOM and re-establishing renderer connections.',
        tip: 'Use this when resuming work on a project after an extended idle period.',
        category: 'Performance & Memory',
        icon: 'fa-solid fa-bolt',
        keywords: ['wake', 'reload', 'restore', 'tabs', 'all'],
        action: () => wakeAllTabsInWorkspace(state.sessionState.current_workspace)
    },

    // Tools & Notes
    {
        id: 'toggle-notes',
        title: 'Toggle Quick Notes',
        desc: 'Open Markdown notes scratchpad saved to ~/.config/mise-browser/notes.md',
        details: 'Opens an integrated Markdown scratchpad backed by an auto-saving plain text file in your user configuration directory.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-note-sticky',
        actionId: 'toggle-notes',
        keywords: ['notes', 'scratchpad', 'markdown', 'memo', 'write'],
        action: () => window.toggleNotesOverlay && window.toggleNotesOverlay()
    },
    {
        id: 'export-reader-to-notes',
        title: 'Export Article to Quick Notes',
        desc: 'Append active Reader View article or page clipping to ~/.config/mise-browser/notes.md',
        details: 'Distills the active article into clean Markdown and appends it to your persistent quick notes file with citation details.',
        tip: 'Press Ctrl+Shift+E while reading an article to save it to your notes without leaving the page.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-file-export',
        actionId: 'export-reader-to-notes',
        shortcut: 'Ctrl+Shift+E',
        keywords: ['notes', 'export', 'reader', 'article', 'save', 'markdown', 'clipping'],
        action: () => window.exportActiveTabToNotes && window.exportActiveTabToNotes()
    },
    {
        id: 'export-reader-to-external',
        title: 'Export Article to External Note App',
        desc: 'Save active article as standalone Markdown file and open in your preferred notes tool',
        details: 'Exports the active article into ~/.config/mise-browser/clippings/ and launches your configured external note application (Obsidian, Logseq, Neovim, etc.).',
        tip: 'Configure your external note command in Preferences to open clippings directly in your favourite editor.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-arrow-up-right-from-square',
        actionId: 'export-reader-to-external',
        keywords: ['export', 'external', 'obsidian', 'logseq', 'nvim', 'notes', 'clipping'],
        action: () => window.exportActiveTabToExternal && window.exportActiveTabToExternal()
    },
    {
        id: 'open-notes-external',
        title: 'Open Quick Notes in External Application',
        desc: 'Open ~/.config/mise-browser/notes.md in your preferred notes app or system editor',
        details: 'Launches your configured external note command or system default Markdown editor with notes.md.',
        tip: 'Allows editing your notes in Obsidian, Neovim, VS Code, or your desktop Markdown editor.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-arrow-up-right-from-square',
        actionId: 'open-notes-external',
        keywords: ['notes', 'external', 'obsidian', 'open', 'editor', 'markdown'],
        action: () => window.openNotesInExternalApp && window.openNotesInExternalApp()
    },
    {
        id: 'toggle-bookmarks',
        title: 'Bookmarks & Quickmarks Manager',
        desc: 'Open bookmarks manager and single-key jump quickmarks',
        details: 'Displays your saved bookmarks ledger and configured quickmarks with real-time filtering, editing, and deletion controls.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-bookmark',
        actionId: 'toggle-bookmarks',
        keywords: ['bookmarks', 'quickmarks', 'saved', 'favorites'],
        action: () => toggleBookmarksOverlay()
    },
    {
        id: 'add-bookmark',
        title: 'Bookmark Current Page',
        desc: 'Save active webpage title and URL into bookmarks collection',
        details: 'Captures the active tab title and canonical URL, storing it in ~/.config/mise-browser/bookmarks.json for instant recall.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-star',
        actionId: 'add-bookmark',
        keywords: ['bookmark', 'save', 'favorite', 'page', 'add'],
        action: () => addCurrentPageToBookmarks()
    },
    {
        id: 'set-quickmark',
        title: 'Set Quickmark',
        desc: 'Assign active webpage to a single alphanumeric shortcut key',
        details: 'Associates the active URL with a single keyboard character (e.g. g for GitHub), enabling instant one-key navigation.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-bolt-lightning',
        actionId: 'set-quickmark',
        keywords: ['quickmark', 'shortcut', 'letter', 'mark', 'set'],
        action: () => promptQuickmark('set')
    },
    {
        id: 'jump-quickmark',
        title: 'Jump to Quickmark',
        desc: 'Navigate instantly to a designated quickmark key',
        details: 'Prompts for a quickmark identifier key and immediately routes the active webview to the associated URL.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-location-arrow',
        actionId: 'jump-quickmark',
        keywords: ['quickmark', 'jump', 'go', 'navigate', 'key'],
        action: () => promptQuickmark('jump')
    },
    {
        id: 'toggle-downloads',
        title: 'Toggle Downloads Shelf',
        desc: 'Slide out bottom downloads drawer with speed and file links',
        details: 'Opens the download shelf showing real-time progress bars, transfer rates, destination paths, and file opening actions.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-download',
        actionId: 'toggle-downloads',
        keywords: ['downloads', 'files', 'shelf', 'drawer'],
        action: () => window.toggleDownloadShelf && window.toggleDownloadShelf()
    },
    {
        id: 'toggle-devtools',
        title: 'Toggle Active Webview DevTools',
        desc: 'Inspect webpage DOM elements, console logs, and network traffic',
        details: 'Opens Chrome Developer Tools docked or detached for the active webview guest frame to inspect elements and debug scripts.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-bug',
        actionId: 'toggle-devtools',
        keywords: ['devtools', 'inspect', 'console', 'debug', 'elements'],
        action: () => window.toggleActiveDevTools && window.toggleActiveDevTools()
    },
    {
        id: 'toggle-global-media',
        title: 'Toggle Global Media Playback',
        desc: 'Play or pause audio and video streams across all workspaces',
        details: 'Broadcasts a play/pause toggle signal to media elements across all active webviews, allowing background audio control.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-play',
        actionId: 'toggle-global-media',
        keywords: ['media', 'play', 'pause', 'music', 'video', 'stream'],
        action: () => window.toggleGlobalMediaPlayback && window.toggleGlobalMediaPlayback()
    },
    {
        id: 'toggle-tab-audio-mute',
        title: 'Toggle Tab Audio Mute',
        desc: 'Mute or unmute audio output for the active tab',
        details: 'Toggles audio mute state for the active tab without pausing video or audio playback.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-volume-xmark',
        actionId: 'toggle-tab-audio-mute',
        keywords: ['mute', 'audio', 'sound', 'volume', 'silence', 'unmute', 'speaker'],
        action: () => window.toggleActiveTabAudioMute && window.toggleActiveTabAudioMute()
    },
    {
        id: 'toggle-global-audio-mute',
        title: 'Toggle Global Audio Mute',
        desc: 'Mute or unmute audio output across all tabs and workspaces',
        details: 'Silences all web audio and media output across all open tabs simultaneously.',
        tip: 'Press M in NORMAL mode to toggle global audio mute.',
        category: 'Tools & Notes',
        icon: 'fa-solid fa-volume-xmark',
        actionId: 'toggle-global-audio-mute',
        keywords: ['mute', 'audio', 'sound', 'volume', 'silence', 'global', 'all', 'panictab'],
        action: () => window.toggleGlobalAudioMute && window.toggleGlobalAudioMute()
    },

    // User Scripts & Styles
    {
        id: 'open-scripts-dir',
        title: 'Open User Scripts Directory',
        desc: 'Browse local ~/.config/mise-browser/scripts in file manager',
        details: 'Launches your system file manager in the local user scripts directory where Tampermonkey-compatible scripts reside.',
        tip: 'Drop .user.js files here to automate page behaviours and customise web applications.',
        category: 'User Scripts & Styles',
        icon: 'fa-solid fa-file-code',
        keywords: ['scripts', 'userscript', 'tampermonkey', 'js', 'javascript', 'folder', 'directory'],
        action: () => window.miseAPI && window.miseAPI.openUserScriptsDir && window.miseAPI.openUserScriptsDir()
    },
    {
        id: 'open-styles-dir',
        title: 'Open User Styles Directory',
        desc: 'Browse local ~/.config/mise-browser/styles in file manager',
        details: 'Opens the user styles directory in your file manager for injecting custom CSS stylesheets into matched domains.',
        tip: 'Drop .user.css files here to create dark themes or tweak styling for any website.',
        category: 'User Scripts & Styles',
        icon: 'fa-solid fa-paintbrush',
        keywords: ['styles', 'userstyle', 'stylish', 'css', 'folder', 'directory', 'theme'],
        action: () => window.miseAPI && window.miseAPI.openUserStylesDir && window.miseAPI.openUserStylesDir()
    },
    {
        id: 'open-script-storage-dir',
        title: 'Open User Script Storage Directory',
        desc: 'Browse local ~/.config/mise-browser/script-storage in file manager',
        details: 'Opens the directory containing persistent JSON key-value stores used by GM_setValue and GM_getValue in user scripts.',
        tip: 'Each script stores its isolated state as an individual JSON file here.',
        category: 'User Scripts & Styles',
        icon: 'fa-solid fa-database',
        keywords: ['storage', 'gm', 'userscript', 'values', 'database', 'json', 'folder', 'directory'],
        action: () => window.miseAPI && window.miseAPI.openUserScriptStorageDir && window.miseAPI.openUserScriptStorageDir()
    },
    {
        id: 'reload-user-content',
        title: 'Reload User Scripts and Styles',
        desc: 'Hot-reload all custom user scripts and stylesheet injections',
        details: 'Clears the in-memory script cache and re-scans the user directory, hot-reloading scripts and styles across active webviews.',
        tip: 'Run this command immediately after saving CSS or JS changes to preview them live.',
        category: 'User Scripts & Styles',
        icon: 'fa-solid fa-rotate',
        keywords: ['reload', 'refresh', 'inject', 'userscripts', 'userstyles'],
        action: () => window.miseAPI && window.miseAPI.reloadUserContent && window.miseAPI.reloadUserContent()
    },

    // Preferences & System
    {
        id: 'toggle-private-mode',
        title: 'Toggle Private Browsing Mode',
        desc: 'Switch session partition to in-memory non-persistent storage',
        details: 'Switches the session state to an isolated in-memory partition where cookies, cache, and history are discarded upon exit.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-user-secret',
        actionId: 'toggle-private-mode',
        keywords: ['private', 'incognito', 'privacy', 'partition'],
        action: () => {
            const next = !state.globalPrivateModeActive;
            state.globalPrivateModeActive = next;
            if (window.handlePrivateBrowsingStateShift) window.handlePrivateBrowsingStateShift(next);
        }
    },
    {
        id: 'test-google-signin',
        title: 'Test Clean Google Sign-In Flow',
        desc: 'Open Google Sign-In in a clean, unauthenticated scratch partition',
        details: 'Purges cached Google auth tokens from the volatile private partition and launches a test tab to verify that Google accepts sign-in without security rejection (rrk=46).',
        tip: 'Use this diagnostic action to audit whether Google authentication works without risking your active workspace logins.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-shield-halved',
        actionId: 'test-google-signin',
        keywords: ['google', 'auth', 'signin', 'login', 'test', 'clean', 'diagnostic', 'rrk46'],
        action: async () => {
            if (window.testCleanGoogleSignIn) {
                await window.testCleanGoogleSignIn();
            }
        }
    },
    {
        id: 'toggle-dark-reader',
        title: 'Toggle Dark Reader on Active Tab',
        desc: 'Invert web page colours into high-contrast dark theme preserving media',
        details: 'Applies zero-dependency GPU-accelerated dark styling to the active tab and split panes.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Web Interaction',
        icon: 'fa-solid fa-circle-half-stroke',
        actionId: 'toggle-dark-reader',
        keywords: ['dark', 'reader', 'mode', 'invert', 'contrast', 'night'],
        action: () => window.toggleDarkReaderOnActiveTab && window.toggleDarkReaderOnActiveTab()
    },
    {
        id: 'toggle-reader-view',
        title: 'Toggle Reader View on Active Tab',
        desc: 'Distil web article into clean, distraction-free reading layout',
        details: 'Extracts article text, byline, and media using in-engine Mozilla Readability with custom themes and typography.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Web Interaction',
        icon: 'fa-solid fa-book-open',
        actionId: 'toggle-reader-view',
        keywords: ['reader', 'view', 'read', 'article', 'distill', 'clean', 'typography', 'book'],
        action: () => window.toggleReaderViewOnActiveTab && window.toggleReaderViewOnActiveTab()
    },
    {
        id: 'toggle-theme',
        title: 'Toggle Colour Theme (Dark / Light)',
        desc: 'Switch browser interface and webview styling between dark and light',
        details: 'Swaps UI theme variables and inverts webview rendering shaders to match your preferred ambient lighting.',
        tip: 'Quickly adapt screen contrast when working late or in brightly lit environments.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-circle-half-stroke',
        keywords: ['theme', 'dark', 'light', 'mode', 'colour', 'color'],
        action: () => window.toggleInterfaceTheme && window.toggleInterfaceTheme()
    },
    {
        id: 'toggle-menu-bar',
        title: 'Open Native Settings Menu',
        desc: 'Open the native browser settings menu',
        details: 'Opens the native menu containing browser settings and application controls.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-window-maximize',
        actionId: 'toggle-menu-bar',
        keywords: ['menu', 'bar', 'top', 'native', 'window'],
        action: () => window.miseAPI && typeof window.miseAPI.toggleMenuBar === 'function' && window.miseAPI.toggleMenuBar()
    },
    {
        id: 'check-for-updates',
        title: 'Check for Updates',
        desc: 'Check GitHub Releases for newer Mise versions and release notes',
        details: 'Queries the GitHub release ledger to check if a newer Mise version is available, providing direct links to release notes and binary packages.',
        tip: 'Mise tracks upstream Electron and Chromium security releases twice weekly.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-arrows-rotate',
        keywords: ['update', 'upgrade', 'version', 'release', 'github', 'check'],
        action: () => window.checkForUpdates && window.checkForUpdates(true)
    },
    {
        id: 'show-whats-new',
        title: "Show What's New",
        desc: 'Open release highlights for this version in a new tab',
        details: 'Opens the offline release highlights page covering recent features, performance improvements, and security updates.',
        tip: 'Use the binding shown here, or customise this action in Keyboard controls.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-sparkles',
        keywords: ['whats', 'new', 'update', 'changelog', 'release', 'notes', 'version'],
        action: () => {
            if (typeof window.openWhatsNewTab === 'function') {
                window.openWhatsNewTab();
            } else if (window.miseAPI && typeof window.miseAPI.getWhatsNewPath === 'function') {
                const url = window.miseAPI.getWhatsNewPath();
                if (url) spawnTabWithUrl(url);
            }
        }
    },
    {
        id: 'open-diagnostics',
        title: 'Open Diagnostics & Health Hub',
        desc: 'View system health, memory utilisation, logs, and generate sanitised bug reports',
        details: 'Opens the Diagnostics modal overlay displaying Electron runtime versions, GPU acceleration states, process memory breakdown, and volatile in-memory logs.',
        tip: 'Zero telemetry: all diagnostic statistics remain 100% local to your machine.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-stethoscope',
        actionId: 'open-diagnostics',
        keywords: ['diagnostics', 'diag', 'health', 'bug', 'report', 'issue', 'memory', 'logs', 'system'],
        action: () => window.toggleDiagnosticsView && window.toggleDiagnosticsView()
    },
    {
        id: 'toggle-diagnostics-recording',
        title: 'Toggle Diagnostics Recording (In-Memory)',
        desc: 'Enable or disable local in-memory diagnostic event logging (OFF by default)',
        details: 'Controls the local ring buffer for capturing renderer errors and navigation warnings. Never transmits data across the network.',
        tip: 'Toggle recording on when reproducing issues before generating an issue report.',
        category: 'Preferences & System',
        icon: 'fa-solid fa-clipboard-check',
        actionId: 'toggle-diagnostics-recording',
        keywords: ['diagnostics', 'recording', 'telemetry', 'privacy', 'logs', 'debug'],
        action: () => window.toggleDiagnosticsRecording && window.toggleDiagnosticsRecording()
    }
];

export const commandRegistry = {};
export const commandActions = {};
COMMAND_DEFINITIONS.forEach(cmd => {
    const id = cmd.actionId || cmd.id;
    commandActions[id] = cmd.action;
    cmd.action = () => window.miseAPI.executeKeyboardAction(id);
    commandRegistry[cmd.title] = cmd.action;
    commandRegistry[cmd.id] = cmd.action;
});

window.miseAPI.getActionMetadata().then(metadata => {
    for (const command of COMMAND_DEFINITIONS) command.category = metadata[command.actionId || command.id]?.category || command.category;
    for (const [id, info] of Object.entries(metadata)) {
        if (COMMAND_DEFINITIONS.some(command => command.id === id || command.actionId === id)) continue;
        const command = {id, actionId: id, title: info.label, desc: info.category,
            details: info.label, tip: 'Customise this action in Keyboard controls.', category: info.category,
            icon: 'fa-solid fa-keyboard', keywords: [id, info.label],
            action: () => window.miseAPI.executeKeyboardAction(id)};
        COMMAND_DEFINITIONS.push(command);
        commandRegistry[id] = command.action;
    }
    const container = document.getElementById('PaletteCategoryFilters');
    if (container) {
        const categories = ['all', ...new Set(COMMAND_DEFINITIONS.map(command => command.category))];
        container.replaceChildren(...categories.map(category => {
            const button = document.createElement('button'); button.type = 'button';
            button.className = 'palette-filter-chip' + (category === 'all' ? ' active' : '');
            button.dataset.category = category; button.textContent = category === 'all' ? 'All' : category;
            return button;
        }));
    }
});

let dynamicKeybinds = {};
let dynamicNormalKeys = {};
window.miseAPI.onKeyboardConfigChanged(config => { dynamicKeybinds = config.shortcuts; dynamicNormalKeys = config.normal; });

async function loadDynamicKeybinds() {
    if (window.miseAPI && typeof window.miseAPI.getKeybinds === 'function') {
        try {
            const config = await window.miseAPI.getKeyboardConfig();
            dynamicKeybinds = config.shortcuts; dynamicNormalKeys = config.normal;
        } catch (e) {}
    }
}

function getCommandShortcut(cmd) {
    if (cmd.id === 'terminal-fallback') return 'Enter';
    const id = cmd.actionId || cmd.id;
    return [...(dynamicKeybinds[id] || []), ...(dynamicNormalKeys[id] || []).map(key => window.MiseKeyboard.display(key, 'normal') + ' (NORMAL)')];
}

function formatShortcutBadges(shortcut) {
    if (!shortcut) return '';
    const list = Array.isArray(shortcut) ? shortcut : [shortcut];
    return list.map(b => {
        const text = String(b).replace(/\+/g, ' + ');
        return `<kbd class="shortcut-badge">${escapeHtml(text)}</kbd>`;
    }).join('<span class="shortcut-sep">/</span>');
}

function recordRecentCommand(cmdId) {
    if (!cmdId) return;
    try {
        const stored = JSON.parse(localStorage.getItem('mise_recent_commands') || '[]');
        const updated = [cmdId, ...stored.filter(id => id !== cmdId)].slice(0, 4);
        localStorage.setItem('mise_recent_commands', JSON.stringify(updated));
    } catch (e) {}
}

function getRecentCommands() {
    try {
        const stored = JSON.parse(localStorage.getItem('mise_recent_commands') || '[]');
        return stored.map(id => COMMAND_DEFINITIONS.find(c => c.id === id)).filter(Boolean);
    } catch (e) {
        return [];
    }
}

let preferencesActive = false;
let paletteRequest = 0;

export async function syncPreferencesUI() {
    if (!window.miseAPI || typeof window.miseAPI.getBrowserSettings !== 'function') return;

    let cfg = null;
    try {
        cfg = await window.miseAPI.getBrowserSettings();
        if (cfg) {
            const gpuToggle = document.getElementById('setting-gpu-toggle');
            const throttleToggle = document.getElementById('setting-throttling-toggle');
            const processSelect = document.getElementById('setting-process-limit');
            const emailSelect = document.getElementById('setting-email-handler');
            const sleepSelect = document.getElementById('setting-sleep-timeout');
            const sidebarToggle = document.getElementById('setting-sidebar-collapse-toggle');
            const spellSelect = document.getElementById('setting-spellchecker-language');

            if (gpuToggle) gpuToggle.checked = !cfg.disable_gpu;
            if (throttleToggle) throttleToggle.checked = !!cfg.background_throttling;
            if (processSelect) processSelect.value = String(cfg.process_limit || 3);
            if (emailSelect) emailSelect.value = cfg.email_handler || 'system';
            if (sleepSelect) sleepSelect.value = String(cfg.tab_sleep_timeout_minutes ?? 15);
            if (sidebarToggle) sidebarToggle.checked = cfg.sidebar_auto_collapse !== false;
            if (spellSelect) spellSelect.value = cfg.spellchecker_language || 'en-GB';

            const statusBarToggle = document.getElementById('setting-status-bar-toggle');
            if (statusBarToggle) statusBarToggle.checked = cfg.show_status_bar !== false;

            const clockToggle = document.getElementById('setting-clock-toggle');
            if (clockToggle) clockToggle.checked = cfg.show_clock !== false;

            const windowControlsToggle = document.getElementById('setting-window-controls-toggle');
            if (windowControlsToggle) windowControlsToggle.checked = !!cfg.show_window_controls;

            const diagToggle = document.getElementById('setting-diagnostics-toggle');
            if (diagToggle) diagToggle.checked = !!cfg.enable_diagnostics;

            const trustedDomainsField = document.getElementById('setting-trusted-domains');
            if (trustedDomainsField) {
                trustedDomainsField.value = Array.isArray(cfg.trusted_domains) ? cfg.trusted_domains.join('\n') : '';
            }

            const notesCmdField = document.getElementById('setting-notes-command');
            if (notesCmdField) {
                notesCmdField.value = cfg.external_notes_command || '';
            }

            if (cfg.custom_search_aliases && typeof cfg.custom_search_aliases === 'object') {
                state.customSearchAliases = { ...cfg.custom_search_aliases };
            }
            renderSearchAliasesUI();
        }
    } catch (err) {}

    const themeToggle = document.getElementById('setting-theme-toggle');
    if (themeToggle) themeToggle.checked = document.body.classList.contains('dark-mode');

    const privateToggle = document.getElementById('setting-private-toggle');
    if (privateToggle) privateToggle.checked = !!state.globalPrivateModeActive;

    syncVisualSettingsInputs(cfg);
    // The extensions scan reads the disk and process metrics, so it only runs for its own pane.
    if (currentSettingsPane() === 'extensions') syncExtensionsPreferences();
}

export function setupPreferencesListeners() {
    initialiseKeyboardEditor();
    initialiseSettingsNav(pane => {
        if (pane === 'keyboard') showKeyboardEditor(); else hideKeyboardEditor();
        if (pane === 'extensions' && preferencesActive) syncExtensionsPreferences();
    });
    const closeBtn = document.getElementById('CloseSettingsBtn');
    if (closeBtn) {
        closeBtn.onclick = (e) => {
            e.preventDefault();
            togglePreferencesView();
        };
    }

    const overlay = document.getElementById('PreferencesOverlay');
    if (overlay) {
        overlay.onkeydown = (e) => {
            if (e.key === 'Escape') {
                e.preventDefault();
                togglePreferencesView();
            }
        };
    }

    const themeToggle = document.getElementById('setting-theme-toggle');
    if (themeToggle) {
        themeToggle.onchange = () => {
            if (window.toggleInterfaceTheme) window.toggleInterfaceTheme();
        };
    }

    setupVisualSettingsListeners();

    const saveBtn = document.getElementById('setting-save-config-btn');
    if (saveBtn) {
        saveBtn.onclick = async () => {
            const gpuToggle = document.getElementById('setting-gpu-toggle');
            const throttleToggle = document.getElementById('setting-throttling-toggle');
            const processSelect = document.getElementById('setting-process-limit');
            const emailSelect = document.getElementById('setting-email-handler');
            const sleepSelect = document.getElementById('setting-sleep-timeout');
            const sidebarToggle = document.getElementById('setting-sidebar-collapse-toggle');
            const spellSelect = document.getElementById('setting-spellchecker-language');
            const windowControlsToggle = document.getElementById('setting-window-controls-toggle');
            const notesCmdField = document.getElementById('setting-notes-command');

            const newCfg = {
                disable_gpu: gpuToggle ? !gpuToggle.checked : false,
                background_throttling: throttleToggle ? throttleToggle.checked : true,
                process_limit: processSelect ? parseInt(processSelect.value, 10) : 3,
                tab_sleep_timeout_minutes: sleepSelect ? parseInt(sleepSelect.value, 10) : 15,
                sidebar_auto_collapse: sidebarToggle ? sidebarToggle.checked : true,
                email_handler: emailSelect ? emailSelect.value : 'system',
                spellchecker_language: spellSelect ? spellSelect.value : 'en-GB',
                show_window_controls: windowControlsToggle ? windowControlsToggle.checked : false,
                external_notes_command: notesCmdField ? notesCmdField.value.trim() : '',
                custom_search_aliases: state.customSearchAliases || {}
            };

            if (window.miseAPI && typeof window.miseAPI.saveBrowserSettings === 'function') {
                await window.miseAPI.saveBrowserSettings(newCfg);
            }
        };
    }

    const liveNotesCmd = document.getElementById('setting-notes-command');
    if (liveNotesCmd) {
        liveNotesCmd.onchange = async () => {
            if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                const cfg = (await window.miseAPI.getBrowserSettings()) || {};
                cfg.external_notes_command = liveNotesCmd.value.trim();
                await window.miseAPI.updateBrowserSettings(cfg);
            }
        };
    }

    const sidebarToggle = document.getElementById('setting-sidebar-collapse-toggle');
    if (sidebarToggle) {
        sidebarToggle.onchange = async () => {
            const isAuto = sidebarToggle.checked;
            if (window.applySidebarMode) window.applySidebarMode(isAuto);
            if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                const cfg = (await window.miseAPI.getBrowserSettings()) || {};
                cfg.sidebar_auto_collapse = isAuto;
                await window.miseAPI.updateBrowserSettings(cfg);
            }
        };
    }

    const statusBarToggle = document.getElementById('setting-status-bar-toggle');
    if (statusBarToggle) {
        statusBarToggle.onchange = () => {
            if (typeof window.toggleStatusBar === 'function') {
                window.toggleStatusBar(statusBarToggle.checked);
            }
        };
    }

    const clockToggle = document.getElementById('setting-clock-toggle');
    if (clockToggle) {
        clockToggle.onchange = () => {
            if (typeof window.toggleClock === 'function') {
                window.toggleClock(clockToggle.checked);
            }
        };
    }

    const windowControlsToggle = document.getElementById('setting-window-controls-toggle');
    if (windowControlsToggle) {
        windowControlsToggle.onchange = () => {
            if (typeof window.toggleWindowControls === 'function') {
                window.toggleWindowControls(windowControlsToggle.checked);
            }
        };
    }

    const privateToggle = document.getElementById('setting-private-toggle');
    if (privateToggle) {
        privateToggle.onchange = (e) => {
            state.globalPrivateModeActive = e.target.checked;
            if (window.handlePrivateBrowsingStateShift) {
                window.handlePrivateBrowsingStateShift(state.globalPrivateModeActive);
            }
        };
    }

    const liveSleepSelect = document.getElementById('setting-sleep-timeout');
    if (liveSleepSelect) {
        liveSleepSelect.onchange = async () => {
            const timeoutMinutes = parseInt(liveSleepSelect.value, 10);
            if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                const cfg = (await window.miseAPI.getBrowserSettings()) || {};
                cfg.tab_sleep_timeout_minutes = timeoutMinutes;
                await window.miseAPI.updateBrowserSettings(cfg);
            }
        };
    }

    const liveSpellSelect = document.getElementById('setting-spellchecker-language');
    if (liveSpellSelect) {
        liveSpellSelect.onchange = async () => {
            const lang = liveSpellSelect.value;
            if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                const cfg = (await window.miseAPI.getBrowserSettings()) || {};
                cfg.spellchecker_language = lang;
                await window.miseAPI.updateBrowserSettings(cfg);
            }
        };
    }

    const liveEmailSelect = document.getElementById('setting-email-handler');
    if (liveEmailSelect) {
        liveEmailSelect.onchange = async () => {
            const handler = liveEmailSelect.value;
            if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                const cfg = (await window.miseAPI.getBrowserSettings()) || {};
                cfg.email_handler = handler;
                await window.miseAPI.updateBrowserSettings(cfg);
            }
        };
    }

    const clearCookiesBtn = document.getElementById('setting-clear-cookies-btn');
    if (clearCookiesBtn) {
        clearCookiesBtn.onclick = () => {
            if (window.executeSurgicalCookieWipe) window.executeSurgicalCookieWipe();
        };
    }

    const clearCacheBtn = document.getElementById('setting-clear-cache-btn');
    if (clearCacheBtn) {
        clearCacheBtn.onclick = () => {
            if (window.executeGlobalCacheWipe) window.executeGlobalCacheWipe();
        };
    }

    const saveTrustedDomainsBtn = document.getElementById('setting-save-trusted-domains-btn');
    if (saveTrustedDomainsBtn) {
        saveTrustedDomainsBtn.onclick = async () => {
            const field = document.getElementById('setting-trusted-domains');
            const note = document.getElementById('setting-trusted-domains-note');
            if (!field || !window.miseAPI || typeof window.miseAPI.updateBrowserSettings !== 'function') return;

            const trustedDomains = field.value
                .split(/[\n,]/)
                .map(d => d.trim().toLowerCase())
                .filter(Boolean);

            try {
                const currentCfg = (typeof window.miseAPI.getBrowserSettings === 'function')
                    ? (await window.miseAPI.getBrowserSettings()) || {}
                    : {};
                const mergedCfg = { ...currentCfg, trusted_domains: trustedDomains };
                await window.miseAPI.updateBrowserSettings(mergedCfg);

                if (note) {
                    note.textContent = 'Saved: applied immediately, no restart needed.';
                    note.classList.add('visible');
                    setTimeout(() => note.classList.remove('visible'), 2500);
                }
            } catch (err) {
                if (note) {
                    note.textContent = 'Failed to save trusted sites.';
                    note.classList.add('visible');
                }
            }
        };
    }

    const diagToggle = document.getElementById('setting-diagnostics-toggle');
    if (diagToggle) {
        diagToggle.onchange = async () => {
            const isEnabled = diagToggle.checked;
            if (window.toggleDiagnosticsRecording) {
                await window.toggleDiagnosticsRecording(isEnabled);
            }
        };
    }

    const openDiagBtn = document.getElementById('setting-open-diagnostics-btn');
    if (openDiagBtn) {
        openDiagBtn.onclick = () => {
            if (window.toggleDiagnosticsView) {
                window.toggleDiagnosticsView();
            }
        };
    }

    setupExtensionsPreferencesListeners();
}

export async function togglePreferencesView() {
    const overlay = document.getElementById('PreferencesOverlay');
    if (!overlay) return;

    preferencesActive = !preferencesActive;
    state.preferencesActive = preferencesActive;
    if (preferencesActive) {
        prepareOverlay('PreferencesOverlay');

        overlay.style.display = 'block';
        if (currentSettingsPane() === 'keyboard') showKeyboardEditor();
        const tab = document.querySelector('#SettingsIndex [aria-selected="true"]');
        (tab || overlay).focus({preventScroll: true});
        await syncPreferencesUI();
    } else {
        overlay.style.display = 'none';
        hideKeyboardEditor();
        clearSettingsSearch();
        returnFocusToWebview();
    }
}

// Opens Preferences on a given pane, or switches pane when already open.
export async function openPreferencesPane(pane) {
    showSettingsPane(pane);
    if (!preferencesActive) await togglePreferencesView();
}

let activeCategoryFilter = 'all';

export function closeCommandPalette() {
    if (!state.paletteActive) return;
    paletteRequest++;
    state.paletteActive = false;
    const overlay = document.getElementById('CommandPaletteOverlay');
    if (overlay) overlay.style.display = 'none';
    returnFocusToWebview();
}

export async function openCommandPalette(category = 'all') {
    const overlay = document.getElementById('CommandPaletteOverlay');
    const input = document.getElementById('PaletteInput');
    if (!overlay || !input) return;

    const request = ++paletteRequest;
    state.paletteActive = true;
    prepareOverlay('CommandPaletteOverlay');
    overlay.style.display = 'flex';
    input.value = '';
    activeCategoryFilter = category;
    filterPaletteCommands('');
    input.focus();
    await loadDynamicKeybinds();
    await prepareExtensionPaletteCommands();
    if (!state.paletteActive || request !== paletteRequest) return;

    activeCategoryFilter = category;
    const filterContainer = document.getElementById('PaletteCategoryFilters');
    if (filterContainer) {
        filterContainer.querySelectorAll('.palette-filter-chip').forEach(chip => {
            chip.classList.toggle('active', chip.dataset.category === category);
        });
    }

    filterPaletteCommands(input.value);
}

export async function toggleCommandPaletteView() {
    if (state.paletteActive) {
        closeCommandPalette();
    } else {
        await openCommandPalette();
    }
}

export function renderPaletteInspector(cmd) {
    const card = document.getElementById('PaletteInspectorCard');
    if (!card) return;

    if (!cmd) {
        card.innerHTML = `
            <div class="palette-inspector-empty">
                <i class="fa-solid fa-magnifying-glass" style="font-size: 32px; opacity: 0.35;"></i>
                <p>Select or hover over a command to view detailed feature documentation, usage guidelines, and keyboard shortcuts.</p>
            </div>
        `;
        return;
    }

    if (cmd.id === 'terminal-fallback') {
        card.innerHTML = `
            <div class="palette-inspector-hero">
                <div class="palette-inspector-icon">
                    <i class="fa-solid fa-terminal"></i>
                </div>
                <div class="palette-inspector-heading">
                    <h2 class="palette-inspector-title">Terminal Oversight</h2>
                    <span class="palette-inspector-badge">Air-Gapped Shell</span>
                </div>
            </div>

            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Command</span>
                <p class="palette-inspector-text">${escapeHtml(cmd.title)}</p>
            </div>

            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Architecture</span>
                <p class="palette-inspector-text">${escapeHtml(cmd.details || cmd.desc)}</p>
            </div>

            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Workflow Tip</span>
                <div class="palette-inspector-tip">${escapeHtml(cmd.tip || 'Press Enter to execute in external terminal.')}</div>
            </div>

            <button type="button" class="palette-inspector-execute-btn" id="PaletteInspectorExecuteBtn">
                <i class="fa-solid fa-play"></i>
                <span>Execute Terminal Command</span>
            </button>
        `;
    } else {
        const shortcut = getCommandShortcut(cmd);
        const shortcutHtml = formatShortcutBadges(shortcut);
        const categoryName = cmd.category || cmd.displayCategory || 'General';

        card.innerHTML = `
            <div class="palette-inspector-hero">
                <div class="palette-inspector-icon">
                    <i class="${cmd.icon}"></i>
                </div>
                <div class="palette-inspector-heading">
                    <h2 class="palette-inspector-title">${escapeHtml(cmd.title)}</h2>
                    <span class="palette-inspector-badge">${escapeHtml(categoryName)}</span>
                </div>
            </div>

            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Keyboard Shortcut</span>
                <div class="palette-inspector-shortcut-row">
                    <div class="palette-inspector-keys">${shortcutHtml || '<span style="opacity: 0.5; font-size: 11px;">No shortcut assigned</span>'}</div>
                    ${cmd.actionId ? `<span class="palette-inspector-key-action">${escapeHtml(cmd.actionId)}</span>` : ''}
                </div>
            </div>

            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Feature Overview</span>
                <p class="palette-inspector-text">${escapeHtml(cmd.details || cmd.desc || 'No additional details available.')}</p>
            </div>

            ${cmd.tip ? `
            <div class="palette-inspector-section">
                <span class="palette-inspector-label">Workflow Tip</span>
                <div class="palette-inspector-tip">${escapeHtml(cmd.tip)}</div>
            </div>
            ` : ''}

            <button type="button" class="palette-inspector-execute-btn" id="PaletteInspectorExecuteBtn">
                <i class="fa-solid fa-play"></i>
                <span>Execute Action</span>
            </button>
        `;
    }

    const execBtn = document.getElementById('PaletteInspectorExecuteBtn');
    if (execBtn) {
        execBtn.onclick = (e) => {
            e.preventDefault();
            executePaletteSelection(document.getElementById('PaletteInput')?.value.trim() || '');
        };
    }
}

export function filterPaletteCommands(filterText) {
    const search = String(filterText || '').toLowerCase().trim();
    const listContainer = document.getElementById('PaletteList');
    const countBadge = document.getElementById('PaletteMatchesCount');
    if (!listContainer) return;
    
    listContainer.innerHTML = '';
    
    const allCommands = COMMAND_DEFINITIONS.concat(getExtensionPaletteCommands());
    const candidateCommands = (activeCategoryFilter === 'all')
        ? allCommands
        : allCommands.filter(cmd => cmd.category === activeCategoryFilter);

    let matches = [];
    if (!search) {
        if (activeCategoryFilter === 'all') {
            const recent = getRecentCommands();
            if (recent.length > 0) {
                recent.forEach(cmd => {
                    matches.push({ ...cmd, displayCategory: 'Recently Used' });
                });
            }
        }
        candidateCommands.forEach(cmd => {
            matches.push({ ...cmd, displayCategory: cmd.category });
        });
    } else {
        candidateCommands.forEach(cmd => {
            const matchTitle = cmd.title.toLowerCase().includes(search);
            const matchDesc = cmd.desc.toLowerCase().includes(search);
            const matchDetails = (cmd.details || '').toLowerCase().includes(search);
            const matchCategory = cmd.category.toLowerCase().includes(search);
            const matchKeywords = Array.isArray(cmd.keywords) && cmd.keywords.some(k => k.toLowerCase().includes(search));
            const sc = getCommandShortcut(cmd);
            const matchShortcut = Array.isArray(sc)
                ? sc.some(s => s.toLowerCase().includes(search))
                : (typeof sc === 'string' && sc.toLowerCase().includes(search));

            if (matchTitle || matchDesc || matchDetails || matchCategory || matchKeywords || matchShortcut) {
                matches.push({ ...cmd, displayCategory: cmd.category });
            }
        });
        
        if (matches.length === 0 && search) {
            matches.push({
                id: 'terminal-fallback',
                title: `Run in terminal: "${filterText}"`,
                desc: 'Pass command to air-gapped terminal protocol on Enter',
                details: 'Executes this input string in your external desktop terminal via the isolated oversight security protocol.',
                tip: 'Press Enter or click Execute to copy and invoke the terminal command.',
                displayCategory: 'Terminal Oversight',
                category: 'Terminal',
                icon: 'fa-solid fa-terminal',
                shortcut: 'Enter',
                action: () => {
                    executeTerminalFallback(filterText);
                }
            });
        }
    }
    
    state.paletteMatches = matches;
    if (countBadge) {
        if (!search) {
            countBadge.textContent = `${matches.length} commands`;
        } else {
            countBadge.textContent = `${matches.length} ${matches.length === 1 ? 'match' : 'matches'}`;
        }
    }
    
    if (matches.length === 0) {
        renderPaletteInspector(null);
        return;
    }

    let currentCategory = '';
    let itemIndex = 0;
    
    matches.forEach((cmd) => {
        if (cmd.displayCategory !== currentCategory) {
            currentCategory = cmd.displayCategory;
            const header = document.createElement('li');
            header.className = 'palette-category-header';
            header.textContent = currentCategory;
            listContainer.appendChild(header);
        }
        
        const li = document.createElement('li');
        li.className = 'palette-item';
        li.dataset.idx = String(itemIndex);
        
        const shortcut = getCommandShortcut(cmd);
        const shortcutHtml = formatShortcutBadges(shortcut);
        
        li.innerHTML = `
            <div class="palette-item-icon">
                <i class="${cmd.icon}"></i>
            </div>
            <span class="palette-item-title">${escapeHtml(cmd.title)}</span>
            <div class="palette-item-meta">
                <span class="palette-item-category">${escapeHtml(cmd.category || cmd.displayCategory)}</span>
                <span class="palette-item-shortcut">${shortcutHtml}</span>
            </div>
        `;
        
        const capturedIdx = itemIndex;
        li.addEventListener('mouseenter', () => {
            state.paletteSelectionIdx = capturedIdx;
            updatePaletteVisualSelection(false);
        });
        li.addEventListener('click', () => {
            state.paletteSelectionIdx = capturedIdx;
            executePaletteSelection(document.getElementById('PaletteInput').value.trim());
        });
        
        listContainer.appendChild(li);
        itemIndex++;
    });
    
    state.paletteSelectionIdx = 0;
    updatePaletteVisualSelection(true);
}

export function updatePaletteVisualSelection(scrollIntoView = true) {
    const items = document.querySelectorAll('#PaletteList .palette-item');
    items.forEach((item, idx) => {
        if (idx === state.paletteSelectionIdx) {
            item.classList.add('selected');
            if (scrollIntoView) {
                item.scrollIntoView({ block: 'nearest' });
            }
        } else {
            item.classList.remove('selected');
        }
    });

    const activeCmd = state.paletteMatches[state.paletteSelectionIdx];
    renderPaletteInspector(activeCmd);
}

export function handlePaletteInputNavigation(e) {
    const total = state.paletteMatches.length;
    if (e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        closeCommandPalette();
        return;
    } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = (state.paletteSelectionIdx + 1) % total;
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = (state.paletteSelectionIdx - 1 + total) % total;
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'PageDown') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = Math.min(total - 1, state.paletteSelectionIdx + 5);
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'PageUp') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = Math.max(0, state.paletteSelectionIdx - 5);
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'Home') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = 0;
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'End') {
        e.preventDefault();
        if (total > 0) {
            state.paletteSelectionIdx = total - 1;
            updatePaletteVisualSelection(true);
        }
    } else if (e.key === 'Enter') {
        e.preventDefault();
        executePaletteSelection(document.getElementById('PaletteInput').value.trim());
    }
}

function executeTerminalFallback(rawInputText) {
    if (!rawInputText) return;
    let finalCommandToCopy = rawInputText;
    const lowerInput = rawInputText.toLowerCase();
    const isRawWebUrl = lowerInput.startsWith('http://') || lowerInput.startsWith('https://');
    const isDangerousSysCall = lowerInput.startsWith('sudo ') || lowerInput.startsWith('curl ') || lowerInput.startsWith('wget ');

    if (isTargetScript(rawInputText) || isRawWebUrl || isDangerousSysCall) {
        finalCommandToCopy = `oversight ${rawInputText}`;
    }

    if (window.miseAPI && typeof window.miseAPI.executeTerminalCommand === 'function') {
        window.miseAPI.executeTerminalCommand(finalCommandToCopy);
    }
}

export function executePaletteSelection(rawInputText) {
    const targetCommand = state.paletteMatches[state.paletteSelectionIdx];
    
    closeCommandPalette();

    if (targetCommand) {
        if (targetCommand.id !== 'terminal-fallback') {
            recordRecentCommand(targetCommand.id);
        }
        if (typeof targetCommand.action === 'function') {
            targetCommand.action();
            if (!state.preferencesActive && !state.dashboardActive && !state.notesActive && !state.bookmarksActive && !state.historyActive && !state.diagnosticsActive) returnFocusToWebview();
            return;
        }
        if (typeof targetCommand === 'string' && commandRegistry[targetCommand]) {
            commandRegistry[targetCommand]();
            returnFocusToWebview();
            return;
        }
    }

    if (rawInputText) {
        executeTerminalFallback(rawInputText);
    }
    returnFocusToWebview();
}

export function setupCommandPaletteOverlayListeners() {
    const overlay = document.getElementById('CommandPaletteOverlay');
    if (overlay) {
        overlay.addEventListener('mousedown', (e) => {
            if (e.target === overlay) {
                closeCommandPalette();
            }
        });
        overlay.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                closeCommandPalette();
            }
        });
    }

    const closeBtn = document.getElementById('ClosePaletteBtn');
    if (closeBtn) {
        closeBtn.onclick = (e) => {
            e.preventDefault();
            e.stopPropagation();
            closeCommandPalette();
        };
    }

    const filterContainer = document.getElementById('PaletteCategoryFilters');
    if (filterContainer) {
        filterContainer.addEventListener('click', (e) => {
            const chip = e.target.closest('.palette-filter-chip');
            if (!chip) return;
            activeCategoryFilter = chip.dataset.category || 'all';
            filterContainer.querySelectorAll('.palette-filter-chip').forEach(c => {
                c.classList.toggle('active', c === chip);
            });
            const input = document.getElementById('PaletteInput');
            filterPaletteCommands(input ? input.value : '');
            if (input) input.focus();
        });
    }

    const listContainer = document.getElementById('PaletteList');
    if (listContainer) {
        listContainer.addEventListener('keydown', handlePaletteInputNavigation);
    }
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        setupPreferencesListeners();
        setupCommandPaletteOverlayListeners();
    });
} else {
    setupPreferencesListeners();
    setupCommandPaletteOverlayListeners();
}
