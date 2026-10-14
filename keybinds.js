// keybinds.js
// Centralised action registry and keybind configuration manager for Mise Browser

const fs = require('fs');
const path = require('path');

const DEFAULT_KEYBINDS = {
    'toggle-menu-bar': 'F1',
    'spawn-tab': 'Ctrl+T',
    'toggle-address': 'Ctrl+L',
    'toggle-dashboard': 'Ctrl+Shift+W',
    'reload-active-tab': ['Ctrl+R', 'F5'],
    'remove-tab': 'Ctrl+X',
    'focus-sidebar': 'Ctrl+S',
    'focus-nav-buttons': ['Ctrl+Shift+N', 'Ctrl+Tab'],
    'focus-webview': 'Ctrl+W',
    'trigger-hints': 'Ctrl+F',
    'toggle-help': 'Ctrl+H',
    'toggle-private-mode': 'Ctrl+Shift+P',
    'toggle-palette': 'Ctrl+P',
    'toggle-find': ['Ctrl+Shift+F', 'F3'],
    'toggle-devtools': ['Ctrl+Shift+I', 'F12'],
    'toggle-notes': 'Ctrl+N',
    'toggle-zen-mode': 'Ctrl+Shift+Z',
    'set-quickmark': 'Ctrl+Shift+Q',
    'jump-quickmark': 'Ctrl+J',
    'add-bookmark': 'Ctrl+Shift+A',
    'toggle-bookmarks': 'Ctrl+Shift+B',
    'toggle-global-media': ['Ctrl+Shift+0', 'F10'],
    'toggle-tab-audio-mute': ['Ctrl+Shift+M', 'F8'],
    'toggle-history': 'Ctrl+Shift+H',
    'toggle-downloads': 'Ctrl+Shift+D',
    'toggle-status-bar': 'Ctrl+/',
    'toggle-split': 'Ctrl+Shift+S',
    'switch-split-focus': 'Ctrl+O',
    'swap-split-panes': 'Ctrl+\\',
    'toggle-dark-reader': 'Ctrl+D',
    'toggle-reader-view': 'F9',
    'toggle-pin-tab': ['Ctrl+Shift+X', 'F4'],
    'export-reader-to-notes': 'Ctrl+Shift+E',
    'jump-to-tab-1': 'Ctrl+1',
    'jump-to-tab-2': 'Ctrl+2',
    'jump-to-tab-3': 'Ctrl+3',
    'jump-to-tab-4': 'Ctrl+4',
    'jump-to-tab-5': 'Ctrl+5',
    'jump-to-tab-6': 'Ctrl+6',
    'jump-to-tab-7': 'Ctrl+7',
    'jump-to-tab-8': 'Ctrl+8',
    'jump-to-tab-9': 'Ctrl+9'
};

const ACTION_METADATA = {
    'toggle-menu-bar': { label: 'Toggle Mise Settings (Menu Bar)', category: 'System' },
    'spawn-tab': { label: 'New Blank Tab', category: 'Navigation & Workspaces' },
    'toggle-address': { label: 'Toggle Floating Address Bar', category: 'Navigation & Workspaces' },
    'toggle-dashboard': { label: 'Toggle Workspace Dashboard', category: 'Navigation & Workspaces' },
    'reload-active-tab': { label: 'Reload Active Tab', category: 'Navigation & Workspaces' },
    'remove-tab': { label: 'Close Current Tab', category: 'Navigation & Workspaces' },
    'focus-sidebar': { label: 'Focus Sidebar Tab List', category: 'Navigation & Workspaces' },
    'focus-nav-buttons': { label: 'Focus Navigation Buttons', category: 'Navigation & Workspaces' },
    'focus-webview': { label: 'Focus Active Webview', category: 'Navigation & Workspaces' },
    'trigger-hints': { label: 'Toggle Link Hints Overlay', category: 'Web Interaction' },
    'toggle-help': { label: 'Toggle Preferences / Help', category: 'System' },
    'toggle-private-mode': { label: 'Toggle Private Browsing Mode', category: 'System' },
    'toggle-palette': { label: 'Toggle Command Palette', category: 'Navigation & Workspaces' },
    'toggle-find': { label: 'Find In Page', category: 'Web Interaction' },
    'toggle-devtools': { label: 'Toggle Active Webview DevTools', category: 'Web Interaction' },
    'toggle-notes': { label: 'Open Quick Notes Overlay', category: 'Navigation & Workspaces' },
    'toggle-zen-mode': { label: 'Toggle Sidebar Expansion (Expand/Collapse)', category: 'Navigation & Workspaces' },
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
    'export-reader-to-notes': { label: 'Export Reader View to Quick Notes', category: 'Web Interaction' },
    'export-reader-to-external': { label: 'Export Reader View to External App', category: 'Web Interaction' },
    'open-notes-external': { label: 'Open Quick Notes in External Application', category: 'Web Interaction' },
    'jump-to-tab-1': { label: 'Jump to Tab 1', category: 'Navigation & Workspaces' },
    'jump-to-tab-2': { label: 'Jump to Tab 2', category: 'Navigation & Workspaces' },
    'jump-to-tab-3': { label: 'Jump to Tab 3', category: 'Navigation & Workspaces' },
    'jump-to-tab-4': { label: 'Jump to Tab 4', category: 'Navigation & Workspaces' },
    'jump-to-tab-5': { label: 'Jump to Tab 5', category: 'Navigation & Workspaces' },
    'jump-to-tab-6': { label: 'Jump to Tab 6', category: 'Navigation & Workspaces' },
    'jump-to-tab-7': { label: 'Jump to Tab 7', category: 'Navigation & Workspaces' },
    'jump-to-tab-8': { label: 'Jump to Tab 8', category: 'Navigation & Workspaces' },
    'jump-to-tab-9': { label: 'Jump to Tab 9 (or Last Tab)', category: 'Navigation & Workspaces' }
};

let activeKeybinds = { ...DEFAULT_KEYBINDS };
let compiledKeymap = compileKeymap(activeKeybinds);

function normalizeKeyCombination(str) {
    if (!str || typeof str !== 'string') return '';
    const parts = str.split('+').map(p => p.trim()).filter(Boolean);
    let ctrl = false;
    let alt = false;
    let shift = false;
    let meta = false;
    let mainKey = '';

    for (const part of parts) {
        const lower = part.toLowerCase();
        if (lower === 'ctrl' || lower === 'control') ctrl = true;
        else if (lower === 'alt' || lower === 'option') alt = true;
        else if (lower === 'shift') shift = true;
        else if (lower === 'meta' || lower === 'super' || lower === 'cmd' || lower === 'command') meta = true;
        else mainKey = part;
    }

    if (!mainKey) return '';

    const lowerKey = mainKey.toLowerCase();
    let normalized = mainKey;
    if (/^f\d{1,2}$/i.test(mainKey)) normalized = mainKey.toUpperCase();
    else if (lowerKey === 'esc' || lowerKey === 'escape') normalized = 'Escape';
    else if (lowerKey === 'enter' || lowerKey === 'return') normalized = 'Enter';
    else if (lowerKey === 'del' || lowerKey === 'delete') normalized = 'Delete';
    else if (lowerKey === 'tab') normalized = 'Tab';
    else if (lowerKey === 'space') normalized = 'Space';
    else if (mainKey.length === 1) normalized = mainKey.toUpperCase();

    const res = [];
    if (ctrl) res.push('Ctrl');
    if (alt) res.push('Alt');
    if (shift) res.push('Shift');
    if (meta) res.push('Meta');
    res.push(normalized);
    return res.join('+');
}

function inputEventToKeyCombination(input) {
    if (!input || input.type !== 'keyDown') return '';
    const key = input.key;
    if (['Control', 'Shift', 'Alt', 'Meta'].includes(key)) return '';

    const parts = [];
    if (input.control) parts.push('Ctrl');
    if (input.alt) parts.push('Alt');
    if (input.shift) parts.push('Shift');
    if (input.meta) parts.push('Meta');

    let normalizedKey = key;
    if (/^f\d{1,2}$/i.test(key)) {
        normalizedKey = key.toUpperCase();
    } else if (key.toLowerCase() === 'escape') {
        normalizedKey = 'Escape';
    } else if (key.toLowerCase() === 'enter') {
        normalizedKey = 'Enter';
    } else if (key.toLowerCase() === 'delete') {
        normalizedKey = 'Delete';
    } else if (key.toLowerCase() === 'tab') {
        normalizedKey = 'Tab';
    } else if (key === ' ') {
        normalizedKey = 'Space';
    } else if (/^[a-z]$/i.test(key)) {
        normalizedKey = key.toUpperCase();
    } else if (input.code && input.code.startsWith('Digit') && input.code.length === 6) {
        normalizedKey = input.code.slice(5);
    } else if (input.code && input.code.startsWith('Key') && input.code.length === 4) {
        normalizedKey = input.code.slice(3).toUpperCase();
    } else if (key.length === 1) {
        normalizedKey = key.toUpperCase();
    }

    parts.push(normalizedKey);
    return parts.join('+');
}

function compileKeymap(binds) {
    const map = new Map();
    for (const [action, binding] of Object.entries(binds)) {
        if (!binding) continue;
        const list = Array.isArray(binding) ? binding : [binding];
        for (const item of list) {
            const normalized = normalizeKeyCombination(item);
            if (normalized) {
                map.set(normalized, action);
            }
        }
    }
    return map;
}

function mergeKeybinds(defaults, userBinds) {
    const result = { ...defaults };
    if (!userBinds || typeof userBinds !== 'object') return result;

    for (const [action, defBinding] of Object.entries(defaults)) {
        if (userBinds[action] !== undefined) {
            const userVal = userBinds[action];
            if (Array.isArray(defBinding)) {
                if (Array.isArray(userVal)) {
                    result[action] = userVal;
                } else if (typeof userVal === 'string') {
                    const defPrimary = defBinding[0];
                    if (userVal === defPrimary) {
                        result[action] = defBinding;
                    } else {
                        result[action] = [userVal, ...defBinding.slice(1)];
                    }
                }
            } else {
                result[action] = userVal;
            }
        }
    }
    for (const [action, userVal] of Object.entries(userBinds)) {
        if (result[action] === undefined) {
            result[action] = userVal;
        }
    }
    return result;
}

function initializeKeybinds(configDir) {
    const keybindsPath = path.join(configDir, 'keybinds.json');
    try {
        if (!fs.existsSync(configDir)) fs.mkdirSync(configDir, { recursive: true });
        if (!fs.existsSync(keybindsPath)) {
            fs.writeFileSync(keybindsPath, JSON.stringify(DEFAULT_KEYBINDS, null, 4), 'utf8');
            activeKeybinds = { ...DEFAULT_KEYBINDS };
        } else {
            const raw = fs.readFileSync(keybindsPath, 'utf8');
            const parsed = JSON.parse(raw);
            activeKeybinds = mergeKeybinds(DEFAULT_KEYBINDS, parsed);
        }
    } catch (err) {
        console.error('Failed to initialise keybinds configuration, using defaults:', err);
        activeKeybinds = { ...DEFAULT_KEYBINDS };
    }
    compiledKeymap = compileKeymap(activeKeybinds);
    return activeKeybinds;
}

function saveKeybinds(configDir, newBinds) {
    const keybindsPath = path.join(configDir, 'keybinds.json');
    try {
        activeKeybinds = mergeKeybinds(DEFAULT_KEYBINDS, newBinds);
        fs.writeFileSync(keybindsPath, JSON.stringify(activeKeybinds, null, 4), 'utf8');
        compiledKeymap = compileKeymap(activeKeybinds);
        return true;
    } catch (err) {
        console.error('Failed to save keybinds configuration:', err);
        return false;
    }
}

function getActionForInput(input) {
    const combo = inputEventToKeyCombination(input);
    if (!combo) return null;
    return compiledKeymap.get(combo) || null;
}

function getKeybinds() {
    return { ...activeKeybinds };
}

function getActionMetadata() {
    return { ...ACTION_METADATA };
}

module.exports = {
    DEFAULT_KEYBINDS,
    ACTION_METADATA,
    initializeKeybinds,
    saveKeybinds,
    getActionForInput,
    getKeybinds,
    getActionMetadata,
    normalizeKeyCombination,
    inputEventToKeyCombination
};
