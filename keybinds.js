// Canonical browser action catalogue and transactional user configuration.
const fs = require('fs');
const path = require('path');
const keyboard = require('./keyboard');
const DEFAULT_KEYBINDS = {
    'toggle-menu-bar': 'F1',
    'spawn-tab': 'Ctrl+T',
    'toggle-address': 'Ctrl+L',
    'toggle-dashboard': 'Ctrl+Shift+W',
    'reload-active-tab': ['Ctrl+R', 'F5'],
    'remove-tab': 'Ctrl+W',
    'focus-sidebar': 'Alt+S',
    'focus-nav-buttons': 'Alt+N',
    'focus-webview': 'Alt+W',
    'trigger-hints': 'Alt+F',
    'toggle-help': 'Ctrl+H',
    'toggle-private-mode': 'Ctrl+Shift+P',
    'toggle-palette': 'Ctrl+P',
    'toggle-find': ['Ctrl+F', 'F3'],
    'toggle-devtools': ['Ctrl+Shift+I', 'F12'],
    'toggle-notes': 'Ctrl+N',
    'toggle-zen-mode': 'Ctrl+Shift+Z',
    'set-quickmark': 'Ctrl+Shift+Q',
    'jump-quickmark': 'Ctrl+J',
    'add-bookmark': 'Ctrl+D',
    'toggle-bookmarks': 'Ctrl+Shift+B',
    'toggle-global-media': ['Ctrl+Shift+0', 'F10'],
    'toggle-tab-audio-mute': ['Ctrl+Shift+M', 'F8'],
    'toggle-history': 'Ctrl+Shift+H',
    'toggle-downloads': 'Ctrl+Shift+D',
    'toggle-status-bar': 'Ctrl+/',
    'toggle-split': 'Ctrl+Shift+S',
    'switch-split-focus': 'Ctrl+O',
    'swap-split-panes': 'Ctrl+\\',
    'toggle-dark-reader': 'Alt+D',
    'toggle-reader-view': 'F9',
    'toggle-pin-tab': ['Ctrl+Shift+X', 'F4'],
    'export-reader-to-notes': 'Ctrl+Shift+E'
};

const ACTION_METADATA = {
    'toggle-menu-bar': { label: 'Open native settings menu', category: 'System' },
    'spawn-tab': { label: 'New Blank Tab', category: 'Navigation & Workspaces' },
    'toggle-address': { label: 'Toggle Floating Address Bar', category: 'Navigation & Workspaces' },
    'toggle-dashboard': { label: 'Toggle Workspace Dashboard', category: 'Navigation & Workspaces' },
    'reload-active-tab': { label: 'Reload Active Tab', category: 'Navigation & Workspaces' },
    'remove-tab': { label: 'Close Current Tab', category: 'Navigation & Workspaces' },
    'focus-sidebar': { label: 'Toggle Sidebar Focus', category: 'Navigation & Workspaces' },
    'focus-nav-buttons': { label: 'Focus Navigation Buttons', category: 'Navigation & Workspaces' },
    'focus-webview': { label: 'Focus Active Webview', category: 'Navigation & Workspaces' },
    'trigger-hints': { label: 'Toggle Link Hints Overlay', category: 'Web Interaction' },
    'toggle-help': { label: 'Toggle Preferences / Help', category: 'System' },
    'toggle-private-mode': { label: 'Toggle Private Browsing Mode', category: 'System' },
    'toggle-palette': { label: 'Toggle Command Palette', category: 'Navigation & Workspaces' },
    'toggle-find': { label: 'Find In Page', category: 'Web Interaction' },
    'toggle-devtools': { label: 'Toggle Active Webview DevTools', category: 'Web Interaction' },
    'toggle-notes': { label: 'Open Quick Notes Overlay', category: 'Navigation & Workspaces' },
    'toggle-zen-mode': { label: 'Toggle Sidebar Expansion (Pin / Unpin)', category: 'Navigation & Workspaces' },
    'set-quickmark': { label: 'Set Quickmark', category: 'Web Interaction' },
    'jump-quickmark': { label: 'Jump to Quickmark', category: 'Web Interaction' },
    'add-bookmark': { label: 'Add Current Page to Bookmarks', category: 'Web Interaction' },
    'toggle-bookmarks': { label: 'Open Bookmarks and Quickmarks', category: 'Navigation & Workspaces' },
    'toggle-global-media': { label: 'Play/Pause Media Playback Globally', category: 'Web Interaction' },
    'toggle-tab-audio-mute': { label: 'Toggle Audio Mute on Active Tab', category: 'Web Interaction' },
    'toggle-global-audio-mute': { label: 'Toggle Audio Mute Globally (All Tabs)', category: 'Web Interaction' },
    'toggle-history': { label: 'Toggle Actionable History', category: 'Navigation & Workspaces' },
    'toggle-downloads': { label: 'Toggle Downloads Shelf', category: 'Navigation & Workspaces' },
    'toggle-status-bar': { label: 'Toggle Status & Mode Bar', category: 'Navigation & Workspaces' },
    'toggle-clock': { label: 'Toggle Live Clock & Date Display', category: 'Navigation & Workspaces' },
    'cycle-clock-format': { label: 'Cycle Clock Format (Date & Time / Time / Seconds)', category: 'Navigation & Workspaces' },
    'toggle-split': { label: 'Toggle Dual-Split View (Side-by-Side)', category: 'Navigation & Workspaces' },
    'switch-split-focus': { label: 'Switch Split Pane Focus (Pane 1 / Pane 2)', category: 'Navigation & Workspaces' },
    'swap-split-panes': { label: 'Swap Split Panes (Left / Right)', category: 'Navigation & Workspaces' },
    'toggle-dark-reader': { label: 'Toggle Dark Reader on Active Tab', category: 'Web Interaction' },
    'toggle-reader-view': { label: 'Toggle Reader View on Active Tab', category: 'Web Interaction' },
    'toggle-pin-tab': { label: 'Pin / Unpin Active Tab', category: 'Navigation & Workspaces' },
    // Unbound by default so the key stays free for pages; add it to keybinds.json to use it
    'open-extension-actions': { label: 'Open Extension Actions (Popups, Commands, Options)', category: 'Web Interaction' },
    'export-reader-to-notes': { label: 'Export Reader View to Quick Notes', category: 'Web Interaction' },
    'export-reader-to-external': { label: 'Export Reader View to External App', category: 'Web Interaction' },
    'open-notes-external': { label: 'Open Quick Notes in External Application', category: 'Web Interaction' }
};

Object.assign(ACTION_METADATA, {
    "toggle-window-controls": {
        "label": "Toggle Window Controls (Minimise, Maximise, Close)",
        "category": "Preferences & System"
    },
    "mute-tab": {
        "label": "Mute / Unmute Active Tab",
        "category": "Tabs & Navigation"
    },
    "reset-zoom": {
        "label": "Reset Tab Zoom Level",
        "category": "Tabs & Navigation"
    },
    "toggle-sidebar-pin": {
        "label": "Toggle Auto-Collapse Sidebar (Pin / Unpin)",
        "category": "Tabs & Navigation"
    },
    "toggle-split-horizontal": {
        "label": "Toggle Dual-Split View (Stacked)",
        "category": "Split View"
    },
    "cycle-split-orientation": {
        "label": "Cycle Split View Orientation",
        "category": "Split View"
    },
    "close-split-view": {
        "label": "Close Split View",
        "category": "Split View"
    },
    "hibernate-tabs": {
        "label": "Hibernate Inactive Tabs",
        "category": "Performance & Memory"
    },
    "wake-all-tabs": {
        "label": "Wake All Tabs in Workspace",
        "category": "Performance & Memory"
    },
    "open-scripts-dir": {
        "label": "Open User Scripts Directory",
        "category": "User Scripts & Styles"
    },
    "open-styles-dir": {
        "label": "Open User Styles Directory",
        "category": "User Scripts & Styles"
    },
    "open-script-storage-dir": {
        "label": "Open User Script Storage Directory",
        "category": "User Scripts & Styles"
    },
    "reload-user-content": {
        "label": "Reload User Scripts and Styles",
        "category": "User Scripts & Styles"
    },
    "test-google-signin": {
        "label": "Test Clean Google Sign-In Flow",
        "category": "Preferences & System"
    },
    "toggle-theme": {
        "label": "Toggle Colour Theme (Dark / Light)",
        "category": "Preferences & System"
    },
    "check-for-updates": {
        "label": "Check for Updates",
        "category": "Preferences & System"
    },
    "open-diagnostics": {
        "label": "Open Diagnostics & Health Hub",
        "category": "Preferences & System"
    },
    "toggle-diagnostics-recording": {
        "label": "Toggle Diagnostics Recording (In-Memory)",
        "category": "Preferences & System"
    },
    "go-back": {
        "label": "Go back",
        "category": "Navigation & Workspaces"
    },
    "go-forward": {
        "label": "Go forward",
        "category": "Navigation & Workspaces"
    },
    "next-tab": {
        "label": "Next tab",
        "category": "Tabs"
    },
    "previous-tab": {
        "label": "Previous tab",
        "category": "Tabs"
    },
    "duplicate-tab": {
        "label": "Duplicate active tab",
        "category": "Tabs"
    },
    "next-workspace": {
        "label": "Next workspace",
        "category": "Navigation & Workspaces"
    },
    "previous-workspace": {
        "label": "Previous workspace",
        "category": "Navigation & Workspaces"
    },
    "new-workspace": {
        "label": "Create workspace",
        "category": "Navigation & Workspaces"
    },
    "force-reload-tab": {
        "label": "Reload without cache",
        "category": "Web Interaction"
    },
    "stop-loading": {
        "label": "Stop loading",
        "category": "Web Interaction"
    },
    "zoom-in": {
        "label": "Zoom in",
        "category": "Web Interaction"
    },
    "zoom-out": {
        "label": "Zoom out",
        "category": "Web Interaction"
    },
    "yank-url": {
        "label": "Copy page URL",
        "category": "Web Interaction"
    },
    "toggle-passthrough": {
        "label": "Toggle passthrough mode",
        "category": "Modes"
    },
    "enter-insert": {
        "label": "Enter insert mode",
        "category": "Modes"
    },
    "scroll-down": {
        "label": "Scroll down",
        "category": "Page scrolling"
    },
    "scroll-up": {
        "label": "Scroll up",
        "category": "Page scrolling"
    },
    "scroll-left": {
        "label": "Scroll left",
        "category": "Page scrolling"
    },
    "scroll-right": {
        "label": "Scroll right",
        "category": "Page scrolling"
    },
    "half-page-down": {
        "label": "Scroll half a page down",
        "category": "Page scrolling"
    },
    "half-page-up": {
        "label": "Scroll half a page up",
        "category": "Page scrolling"
    },
    "page-top": {
        "label": "Go to page top",
        "category": "Page scrolling"
    },
    "page-bottom": {
        "label": "Go to page bottom",
        "category": "Page scrolling"
    },
    "tab-1": {
        "label": "Select tab 1",
        "category": "Tabs"
    },
    "tab-2": {
        "label": "Select tab 2",
        "category": "Tabs"
    },
    "tab-3": {
        "label": "Select tab 3",
        "category": "Tabs"
    },
    "tab-4": {
        "label": "Select tab 4",
        "category": "Tabs"
    },
    "tab-5": {
        "label": "Select tab 5",
        "category": "Tabs"
    },
    "tab-6": {
        "label": "Select tab 6",
        "category": "Tabs"
    },
    "tab-7": {
        "label": "Select tab 7",
        "category": "Tabs"
    },
    "tab-8": {
        "label": "Select tab 8",
        "category": "Tabs"
    },
    "tab-9": {
        "label": "Select last tab",
        "category": "Tabs"
    }
});

Object.assign(ACTION_METADATA, {
    'show-whats-new': {label: "Show What's New", category: 'System'},
    'toggle-native-menu-visibility': {label: 'Toggle native menu bar visibility', category: 'System'},
    'move-tab-to-workspace': {label: 'Move active tab to another workspace', category: 'Tabs'},
    'open-keyboard-settings': {label: 'Customise keyboard controls', category: 'System'},
    'reopen-closed-tab': {label: 'Reopen last closed tab', category: 'Tabs'},
    'move-tab-left': {label: 'Move tab left', category: 'Tabs'},
    'move-tab-right': {label: 'Move tab right', category: 'Tabs'},
    'toggle-fullscreen': {label: 'Toggle full screen', category: 'System'},
    'print-page': {label: 'Print active page', category: 'Web Interaction'},
    'toggle-notifications': {label: 'Toggle web notifications', category: 'System'},
    'toggle-webview-theme': {label: 'Toggle website light or dark theme', category: 'Web Interaction'},
    'toggle-site-shields': {label: 'Toggle shields for the active site', category: 'Web Interaction'}
});
for (const duplicate of ['mute-tab', 'toggle-sidebar-expansion', 'open-preferences']) delete ACTION_METADATA[duplicate];
Object.assign(DEFAULT_KEYBINDS, {
    'open-keyboard-settings': 'Ctrl+,',
    'reopen-closed-tab': 'Ctrl+Shift+T', 'move-tab-left': 'Ctrl+Shift+PageUp', 'move-tab-right': 'Ctrl+Shift+PageDown',
    'toggle-fullscreen': 'F11',
    'next-tab': ['Ctrl+Tab', 'Ctrl+PageDown'], 'previous-tab': ['Ctrl+Shift+Tab', 'Ctrl+PageUp'],
    'go-back': 'Alt+ArrowLeft', 'go-forward': 'Alt+ArrowRight',
    'force-reload-tab': 'Ctrl+Shift+R', 'zoom-in': ['Ctrl+Plus', 'Ctrl+='],
    'zoom-out': 'Ctrl+-', 'reset-zoom': 'Ctrl+0', 'duplicate-tab': 'Ctrl+Shift+K',
    'next-workspace': 'Alt+PageDown', 'previous-workspace': 'Alt+PageUp',
    'toggle-passthrough': 'Shift+Escape'
});
for (let i = 1; i <= 9; i++) DEFAULT_KEYBINDS[`tab-${i}`] = `Alt+${i}`;
const DEFAULT_NORMAL = {
    'scroll-down': 'J', 'scroll-up': 'K', 'scroll-left': 'H', 'scroll-right': 'L',
    'half-page-down': 'D', 'half-page-up': 'U', 'page-top': 'G G', 'page-bottom': 'Shift+G',
    'spawn-tab': 'T', 'remove-tab': 'X', 'toggle-address': 'O', 'reload-active-tab': 'R',
    'force-reload-tab': 'Shift+R', 'go-back': 'Shift+H', 'go-forward': 'Shift+L',
    'toggle-find': '/', 'trigger-hints': 'F', 'toggle-reader-view': 'E',
    'toggle-tab-audio-mute': 'M', 'toggle-global-audio-mute': 'Shift+M',
    'toggle-pin-tab': 'P', 'toggle-dashboard': 'W', 'focus-sidebar': 'S',
    'yank-url': 'Y', 'enter-insert': 'I'
};
for (const id of Object.keys(ACTION_METADATA)) {
    DEFAULT_KEYBINDS[id] ??= [];
    DEFAULT_NORMAL[id] ??= [];
}
const defaults = { version: 2, shortcuts: DEFAULT_KEYBINDS, normal: DEFAULT_NORMAL };
let active = keyboard.validateConfig(defaults, ACTION_METADATA).config;
let configurationWarning = '';
let compiled = keyboard.compile(active.shortcuts);
const clone = value => JSON.parse(JSON.stringify(value));

function initialiseCandidate(raw) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return {ok: false, error: 'Bindings must be an action table.'};
    if (Object.hasOwn(raw, 'version') && raw.version !== 2) return {ok: false, error: 'Unsupported keyboard configuration version.'};
    const legacy = !Object.hasOwn(raw, 'version');
    const candidate = legacy ? {version: 2, shortcuts: raw, normal: {}} : raw;
    if (!candidate.shortcuts || !candidate.normal || typeof candidate.shortcuts !== 'object' || typeof candidate.normal !== 'object' || Array.isArray(candidate.shortcuts) || Array.isArray(candidate.normal)) return {ok: false, error: 'Invalid binding tables.'};
    const shortcuts = {...DEFAULT_KEYBINDS, ...candidate.shortcuts};
    if (legacy) {
        // Newly introduced defaults must not displace a user's existing legacy assignments.
        const occupied = new Set(Object.values(candidate.shortcuts).flat().filter(value => typeof value === 'string').map(keyboard.normalise));
        for (const id of Object.keys(shortcuts)) if (!Object.hasOwn(candidate.shortcuts, id)) {
            shortcuts[id] = (Array.isArray(shortcuts[id]) ? shortcuts[id] : [shortcuts[id]]).filter(value => !occupied.has(keyboard.normalise(value)));
        }
    }
    return keyboard.validateConfig({version: 2, shortcuts, normal: {...DEFAULT_NORMAL, ...candidate.normal}}, ACTION_METADATA);
}
function initializeKeybinds(configDir) {
    configurationWarning = '';
    try {
        const file = path.join(configDir, 'keybinds.json');
        if (fs.existsSync(file)) {
            const result = initialiseCandidate(JSON.parse(fs.readFileSync(file, 'utf8')));
            if (!result.ok) throw new Error(result.error);
            active = result.config;
        } else active = keyboard.validateConfig(defaults, ACTION_METADATA).config;
    } catch (err) {
        configurationWarning = 'Your bindings file could not be loaded. Defaults are active; the original file has been left untouched. Saving will replace it.';
        console.error('Invalid keyboard configuration; using defaults:', err.message);
        active = keyboard.validateConfig(defaults, ACTION_METADATA).config;
    }
    compiled = keyboard.compile(active.shortcuts);
    return getKeybinds();
}
function saveKeybinds(configDir, candidate) {
    const result = initialiseCandidate(candidate);
    if (!result.ok) return result;
    const file = path.join(configDir, 'keybinds.json');
    const temporary = file + '.tmp';
    try {
        fs.mkdirSync(configDir, {recursive: true});
        fs.writeFileSync(temporary, JSON.stringify(result.config, null, 2) + '\n', {encoding: 'utf8', mode: 0o600});
        fs.renameSync(temporary, file);
        active = result.config;
        configurationWarning = '';
        compiled = keyboard.compile(active.shortcuts);
        return {ok: true, config: getKeyboardConfig()};
    } catch (err) {
        try { fs.unlinkSync(temporary); } catch (_) {}
        return {ok: false, error: 'Could not save bindings. Your previous bindings are still active.'};
    }
}
function getActionForInput(input, includeRepeat = false) {
    if (!input || input.type !== 'keyDown' || input.isComposing) return null;
    const action = compiled.get(keyboard.eventCombo(input));
    if (!includeRepeat && input.isAutoRepeat && !keyboard.repeatable(action)) return null;
    return action || null;
}
function getKeyboardConfig() { return {...clone(active), ...(configurationWarning ? {warning: configurationWarning} : {})}; }
function getKeybinds() { return clone(active.shortcuts); }
function getActionMetadata() { return clone(ACTION_METADATA); }
module.exports = {DEFAULT_KEYBINDS, DEFAULT_NORMAL, ACTION_METADATA, initializeKeybinds,
    saveKeybinds, getActionForInput, getKeybinds, getKeyboardConfig, getActionMetadata,
    getDefaults: () => clone(defaults), normalizeKeyCombination: keyboard.normalise,
    inputEventToKeyCombination: keyboard.eventCombo};
