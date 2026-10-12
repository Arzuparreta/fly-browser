const { app, BrowserWindow, ipcMain, session, Menu, MenuItem, nativeTheme, Notification, clipboard, shell, webContents, dialog, net } = require('electron');
const path = require('path');
const fs = require('fs');
const https = require('https');
const { exec, spawn, execSync, spawnSync } = require('child_process');
const v8 = require('v8');
const vm = require('vm');

// Expose GC in Node main process for memory compaction
try {
    v8.setFlagsFromString(['-', '-', 'expose-gc'].join(''));
    global.gc = vm.runInNewContext('gc');
} catch (e) {
    // Ignore if flag modification is not supported
}

// Single instance lock to prevent duplicate windows when opening external links
const gotSingleInstanceLock = app.requestSingleInstanceLock();
if (!gotSingleInstanceLock) {
    app.quit();
}

// Require the security config module to isolate filtering and hardening rules
const security = require('./security');
const keybinds = require('./keybinds');
const userContent = require('./userContent');
const diagnostics = require('./diagnostics');

// NATIVE CONFIG UTILITIES
const CONFIG_DIR = path.join(app.getPath('home'), '.config', 'mise-browser');
const CONFIG_PATH = path.join(CONFIG_DIR, 'config.json');
const NOTES_PATH = path.join(CONFIG_DIR, 'notes.md');
const BOOKMARKS_PATH = path.join(CONFIG_DIR, 'bookmarks.json');
const QUICKMARKS_PATH = path.join(CONFIG_DIR, 'quickmarks.json');
const sessionPath = path.join(CONFIG_DIR, 'session.json');
const sessionBackupPath = path.join(CONFIG_DIR, 'session.json.bak');
const sentinelPath = path.join(CONFIG_DIR, 'session_sentinel.json');
const historyPath = path.join(CONFIG_DIR, 'history.json');

let isCrashRecovery = false;

function checkAndInitializeSentinel() {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        if (fs.existsSync(sentinelPath)) {
            const raw = fs.readFileSync(sentinelPath, 'utf8');
            const data = JSON.parse(raw);
            if (data && data.clean_exit === false) {
                isCrashRecovery = true;
            }
        }
    } catch (err) {}
    markSentinelState(false);
}

function markSentinelState(cleanExit) {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        const data = {
            clean_exit: !!cleanExit,
            timestamp: Date.now(),
            pid: process.pid
        };
        const tmpPath = sentinelPath + '.tmp';
        fs.writeFileSync(tmpPath, JSON.stringify(data, null, 2), 'utf8');
        fs.renameSync(tmpPath, sentinelPath);
    } catch (err) {}
}

const DEFAULT_THEME_COLORS = {
    dark: {
        accent: '#7aa2f7',
        bg_main: '#1a1b26',
        bg_sidebar: '#16161e',
        text: '#c0caf5',
        sidebar_opacity: 100,
        overlay_opacity: 100
    },
    light: {
        accent: '#2b59c3',
        bg_main: '#e5e5e5',
        bg_sidebar: '#d4d4d4',
        text: '#1a1a1a',
        sidebar_opacity: 100,
        overlay_opacity: 100
    }
};

const DEFAULT_CONFIG = {
    disable_gpu: false,
    background_throttling: true,
    process_limit: 4,
    email_handler: 'system',
    search_engine: 'https://duckduckgo.com/?q=%s',
    spellchecker_language: 'en-GB',
    theme: 'dark',
    webview_theme: 'dark',
    trusted_domains: ['accounts.google.com'],
    tab_sleep_timeout_minutes: 15,
    sidebar_auto_collapse: true,
    show_status_bar: true,
    show_clock: true,
    clock_format: 'datetime',
    theme_colors: { ...DEFAULT_THEME_COLORS },
    enable_diagnostics: false,
    show_window_controls: false,
    last_seen_version: null
};

function loadBrowserConfig() {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        if (!fs.existsSync(CONFIG_PATH)) {
            fs.writeFileSync(CONFIG_PATH, JSON.stringify(DEFAULT_CONFIG, null, 4), 'utf-8');
            return { ...DEFAULT_CONFIG };
        }
        const parsed = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf-8'));
        return {
            ...DEFAULT_CONFIG,
            ...parsed,
            spellchecker_language: parsed.spellchecker_language || 'en-GB',
            webview_theme: parsed.webview_theme || 'dark',
            theme_colors: {
                dark: { ...DEFAULT_THEME_COLORS.dark, ...(parsed.theme_colors?.dark || {}) },
                light: { ...DEFAULT_THEME_COLORS.light, ...(parsed.theme_colors?.light || {}) }
            }
        };
    } catch (e) {
        return { ...DEFAULT_CONFIG };
    }
}

// Initialise keybinds configuration
keybinds.initializeKeybinds(CONFIG_DIR);

function saveBrowserConfig(cfg) {
    try {
        const merged = { ...loadBrowserConfig(), ...cfg };
        fs.writeFileSync(CONFIG_PATH, JSON.stringify(merged, null, 4), 'utf-8');
        app.relaunch();
        app.exit(0);
    } catch (e) {}
}

function initializeEngineSwitches() {
    const cfg = loadBrowserConfig();

    app.commandLine.appendSwitch('force-webrtc-ip-handling-policy', 'default_public_interface_only');

    // Privacy and unwanted Web API feature restrictions
    const disabledFeatures = [
        'Translate', 'PrivacySandboxSettings4',
        'PrivacySandboxAdsAPIsOverride', 'PrivacySandboxAdsAPIsM1Override',
        'PrivacySandboxAdsAPIs', 'BrowsingTopics',
        'BrowsingTopicsDocumentAPI', 'Fledge',
        'FencedFrames', 'SharedStorage', 'PrivateAggregationApi',
        'InterestGroupStorage', 'AttributionReportingCrossAppWeb',
        'WebUSB', 'WebBluetooth', 'Serial',
        'GenericSensor', 'Vulkan',
        'VulkanFromANGLE', 'DefaultANGLEVulkan',
        'WebGPU', 'SkiaGraphite'
    ];
    app.commandLine.appendSwitch('disable-features', disabledFeatures.join(','));
    app.commandLine.appendSwitch('disable-vulkan-surface');

    if (cfg.disable_gpu) {
        app.commandLine.appendSwitch('disable-gpu');
        app.commandLine.appendSwitch('disable-gpu-compositing');
    } else {
        if (process.platform === 'linux') {
            app.commandLine.appendSwitch('ignore-gpu-blocklist');
            app.commandLine.appendSwitch('enable-gpu-rasterization');
            app.commandLine.appendSwitch('enable-accelerated-video-decode');
            // Kept VA-API hardware decode/encode and CanvasOopRasterization for parallel GPU tile rasterization
            app.commandLine.appendSwitch('enable-features', 'VaapiVideoDecoder,VaapiVideoEncoder,CanvasOopRasterization,TLSExtensionGrease');
        } else {
            app.commandLine.appendSwitch('enable-features', 'TLSExtensionGrease');
        }
    }

    if (cfg.background_throttling) {
        app.commandLine.appendSwitch('enable-background-timer-throttling');
        app.commandLine.appendSwitch('add-delay-to-background-timer-tasks');
    }
    
    // Compositor texture boundary allocation
    app.commandLine.appendSwitch('force-gpu-mem-available-mb', '1024');

    app.commandLine.appendSwitch('renderer-process-limit', String(cfg.process_limit || 4));
    app.commandLine.appendSwitch('disable-smooth-scrolling');
    app.commandLine.appendSwitch('enable-strict-mixed-content-checking');
    app.commandLine.appendSwitch('disable-battery-saver');
    app.commandLine.appendSwitch('log-level', '3');
    app.commandLine.appendSwitch('disable-speech-api');

    // Google sign-in needs Electron's own identity, see security.js
    security.setGoogleSignInUserAgent(app.userAgentFallback);

    // Standardise baseline User-Agent string across all renderers and network requests
    const realChromeMajor = (process.versions && process.versions.chrome) ? process.versions.chrome.split('.')[0] : '132';
    app.userAgentFallback = `Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${realChromeMajor}.0.0.0 Safari/537.36`;
}

initializeEngineSwitches();

let mainWindow;
let privateBrowsingEnabled = false;
const MAX_HISTORY_ITEMS = 500;

// URL queue and readiness tracking for external link dispatch
let isRendererReady = false;
const pendingUrls = [];

function extractUrlsFromArgs(argv) {
    if (!argv || !Array.isArray(argv)) return [];
    const urls = [];
    for (let i = 1; i < argv.length; i++) {
        let arg = (argv[i] || '').trim();
        if (!arg) continue;
        if (arg.startsWith('-')) continue;
        arg = arg.replace(/^["']|["']$/g, '');
        if (arg === '.' || arg.endsWith('main.js') || arg === __dirname) continue;

        if (/^https?:\/\//i.test(arg) || /^file:\/\//i.test(arg)) {
            urls.push(arg);
        } else if (/^localhost(:\d+)?(\/.*)?$/i.test(arg) || /^127\.0\.0\.1(:\d+)?(\/.*)?$/i.test(arg)) {
            urls.push('http://' + arg);
        } else if (/^[a-zA-Z0-9][-a-zA-Z0-9]*\.[a-zA-Z]{2,}(\/.*)?$/i.test(arg)) {
            urls.push('https://' + arg);
        } else {
            try {
                if (fs.existsSync(arg) && fs.statSync(arg).isFile() && !arg.endsWith('.js') && !arg.endsWith('.json')) {
                    urls.push('file://' + path.resolve(arg));
                }
            } catch (e) {}
        }
    }
    return urls;
}

function dispatchTabUrl(url) {
    if (!url) return;
    if (isRendererReady && mainWindow && mainWindow.webContents) {
        mainWindow.webContents.send('master-shortcut', 'spawn-tab-with-url', url);
    } else {
        pendingUrls.push(url);
    }
}

function flushPendingUrls() {
    if (!mainWindow || !mainWindow.webContents) return;
    while (pendingUrls.length > 0) {
        const nextUrl = pendingUrls.shift();
        mainWindow.webContents.send('master-shortcut', 'spawn-tab-with-url', nextUrl);
    }
}

// Queue initial command line URLs received on cold start
extractUrlsFromArgs(process.argv).forEach(url => pendingUrls.push(url));


let inMemoryHistory = null;
let historyFlushTimer = null;

function getHistoryCache() {
    if (inMemoryHistory === null) {
        try {
            if (fs.existsSync(historyPath)) {
                inMemoryHistory = JSON.parse(fs.readFileSync(historyPath, 'utf8'));
            } else {
                inMemoryHistory = [];
            }
        } catch (err) {
            inMemoryHistory = [];
        }
    }
    return inMemoryHistory;
}

function flushHistoryToDisk() {
    if (historyFlushTimer) {
        clearTimeout(historyFlushTimer);
        historyFlushTimer = null;
    }
    if (inMemoryHistory === null) return;
    try {
        const dir = path.dirname(historyPath);
        if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(historyPath, JSON.stringify(inMemoryHistory, null, 4), 'utf8');
    } catch (err) {}
}

function scheduleHistoryFlush() {
    if (historyFlushTimer) return;
    historyFlushTimer = setTimeout(() => {
        historyFlushTimer = null;
        flushHistoryToDisk();
    }, 4000);
}

function logVisit(title, url) {
    if (!url || url === 'about:blank' || url.startsWith('file://')) return;

    const history = getHistoryCache();
    if (history.length > 0 && history[0].url === url) return;

    const newEntry = {
        title: title || url,
        url: url,
        timestamp: Date.now()
    };

    history.unshift(newEntry);
    if (history.length > MAX_HISTORY_ITEMS) {
        history.length = MAX_HISTORY_ITEMS;
    }
    scheduleHistoryFlush();
}

function sendSystemNotification(title, body) {
    if (!Notification.isSupported()) return;

    const notification = new Notification({
        title: title,
        body: body,
        icon: path.join(__dirname, 'assets', 'icons', 'Mise-logo256.png'),
        urgency: 'low',
        silent: true
    });

    notification.show();
}

let globalNotificationsEnabled = true;

// Generates a filesystem-safe persistent container partition string for a workspace
function getWorkspacePartition(workspaceName) {
    if (!workspaceName) return 'persist:default';
    const clean = String(workspaceName).trim().toLowerCase().replace(/[^a-z0-9_-]/gi, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');
    return `persist:${clean || 'default'}`;
}

function getSessionForContext(context) {
    if (typeof context === 'string') {
        return session.fromPartition(context);
    }
    if (context && typeof context === 'object') {
        if (context.isPrivate) return session.fromPartition('MisePrivateProfile');
        if (context.partition) return session.fromPartition(context.partition);
        if (context.workspace) return session.fromPartition(getWorkspacePartition(context.workspace));
    }
    if (context === true) return session.fromPartition('MisePrivateProfile');
    return session.defaultSession;
}

const configuredSessions = new WeakSet();
const activeSessions = new Set();
const activeDownloads = new Map();
const recentDownloads = [];
const MAX_RECENT_DOWNLOADS = 20;

function configureDownloads(targetSession) {
    targetSession.on('will-download', (event, item) => {
        const downloadId = `dl_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`;
        let startedNotified = false;
        let lastProgressBytes = 0;
        let lastProgressTime = Date.now();
        let currentSpeed = 0;
        let trackedFd = null;

        function updateTrackedFd(targetPath) {
            if (!targetPath || typeof targetPath !== 'string') return;
            try {
                if (fs.existsSync(targetPath)) {
                    if (trackedFd !== null) {
                        try {
                            const currentLink = fs.readlinkSync(`/proc/self/fd/${trackedFd}`);
                            if (currentLink === targetPath) return;
                            fs.closeSync(trackedFd);
                        } catch (e) {}
                        trackedFd = null;
                    }
                    trackedFd = fs.openSync(targetPath, 'r');
                }
            } catch (e) {}
        }

        function resolveLiveSavePath() {
            const currentPath = item.getSavePath();
            if (currentPath) {
                updateTrackedFd(currentPath);
            }
            if (trackedFd !== null) {
                try {
                    const fdPath = `/proc/self/fd/${trackedFd}`;
                    if (fs.existsSync(fdPath)) {
                        const realPath = fs.readlinkSync(fdPath);
                        if (realPath && !realPath.includes('(deleted)')) {
                            return realPath;
                        }
                    }
                } catch (e) {}
            }
            return currentPath;
        }

        function getDisplayName(savePath, fallbackName) {
            if (savePath && typeof savePath === 'string') {
                const base = path.basename(savePath);
                if (base && base.length > 0) return base;
            }
            return fallbackName || 'download';
        }

        activeDownloads.set(downloadId, {
            id: downloadId,
            item: item,
            filename: item.getFilename(),
            totalBytes: item.getTotalBytes(),
            receivedBytes: item.getReceivedBytes(),
            state: item.getState(),
            savePath: item.getSavePath(),
            startTime: Date.now()
        });

        function notifyStarted() {
            if (startedNotified) return;
            startedNotified = true;
            const currentSavePath = resolveLiveSavePath();
            const displayName = getDisplayName(currentSavePath, item.getFilename());
            if (mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('download-started', {
                    id: downloadId,
                    filename: displayName,
                    totalBytes: item.getTotalBytes(),
                    receivedBytes: item.getReceivedBytes(),
                    state: item.getState(),
                    savePath: currentSavePath
                });
            }
        }

        item.on('updated', (updateEvent, state) => {
            const received = item.getReceivedBytes();
            const total = item.getTotalBytes();

            const savePath = resolveLiveSavePath();
            const displayName = getDisplayName(savePath, item.getFilename());

            const tracked = activeDownloads.get(downloadId);
            if (tracked) {
                tracked.receivedBytes = received;
                tracked.totalBytes = total;
                tracked.state = state;
                tracked.savePath = savePath;
                tracked.filename = displayName;
            }

            if (!startedNotified && (received > 0 || savePath)) {
                notifyStarted();
            }

            if (state === 'progressing') {
                const now = Date.now();
                const timeDelta = (now - lastProgressTime) / 1000;
                if (timeDelta >= 0.4) {
                    const bytesDelta = received - lastProgressBytes;
                    currentSpeed = bytesDelta > 0 ? (bytesDelta / timeDelta) : 0;
                    lastProgressBytes = received;
                    lastProgressTime = now;
                }

                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('download-progress', {
                        id: downloadId,
                        filename: displayName,
                        totalBytes: total,
                        receivedBytes: received,
                        percent: total > 0 ? Math.round((received / total) * 100) : 0,
                        speed: currentSpeed,
                        state: state,
                        savePath: savePath,
                        isPaused: item.isPaused()
                    });
                }
            } else if (state === 'interrupted') {
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('download-progress', {
                        id: downloadId,
                        filename: displayName,
                        totalBytes: total,
                        receivedBytes: received,
                        percent: total > 0 ? Math.round((received / total) * 100) : 0,
                        speed: 0,
                        state: 'interrupted',
                        savePath: savePath,
                        isPaused: false
                    });
                }
            }
        });

        item.once('done', (doneEvent, state) => {
            activeDownloads.delete(downloadId);

            if (!startedNotified && state === 'cancelled') {
                if (trackedFd !== null) {
                    try { fs.closeSync(trackedFd); } catch (e) {}
                    trackedFd = null;
                }
                return;
            }

            if (!startedNotified) {
                notifyStarted();
            }

            const finalSavePath = resolveLiveSavePath();
            if (trackedFd !== null) {
                try { fs.closeSync(trackedFd); } catch (e) {}
                trackedFd = null;
            }

            const finalFilename = getDisplayName(finalSavePath, item.getFilename());

            const finalRecord = {
                id: downloadId,
                filename: finalFilename,
                totalBytes: item.getTotalBytes(),
                receivedBytes: item.getReceivedBytes(),
                state: state,
                savePath: finalSavePath,
                completedAt: Date.now()
            };

            recentDownloads.unshift(finalRecord);
            if (recentDownloads.length > MAX_RECENT_DOWNLOADS) {
                recentDownloads.pop();
            }

            if (mainWindow && !mainWindow.isDestroyed()) {
                mainWindow.webContents.send('download-done', finalRecord);
            }

            if (state === 'completed' && globalNotificationsEnabled) {
                sendSystemNotification('Download Complete', `${finalFilename} has finished downloading.`);
            }
        });
    });
}

function applySpellcheckerToAllSessions(lang) {
    for (const sess of activeSessions) {
        security.applySpellcheckerLanguage(sess, lang);
    }
}

function configureAndHardenSession(targetSession) {
    if (!targetSession) return;
    activeSessions.add(targetSession);
    if (configuredSessions.has(targetSession)) return;
    configuredSessions.add(targetSession);

    const cfg = loadBrowserConfig();
    security.hardenSession(targetSession, cfg.spellchecker_language || 'en-GB');
    configureSessionPermissions(targetSession);
    configureDownloads(targetSession);
}

function configureSessionPermissions(targetSession) {
    const blockedPermissions = ['media', 'geolocation', 'midiSysex', 'audio', 'video'];

    targetSession.setPermissionRequestHandler((webContents, permission, callback) => {
        let hostname = '';
        try { hostname = new URL(webContents.getURL()).hostname; } catch (e) {}
        if (security.isTrustedDomain(hostname)) return callback(true);
        if (permission === 'notifications') return callback(globalNotificationsEnabled);
        if (permission === 'clipboard-read' || permission === 'clipboard-sanitized-write') return callback(true);
        if (blockedPermissions.includes(permission)) return callback(false);
        callback(true);
    });

    targetSession.setPermissionCheckHandler((webContents, permission, origin) => {
        let hostname = '';
        try { hostname = new URL(origin).hostname; } catch (e) {}
        if (security.isTrustedDomain(hostname)) return true;
        if (permission === 'notifications') return globalNotificationsEnabled;
        if (permission === 'clipboard-read' || permission === 'clipboard-sanitized-write') return true;
        return !blockedPermissions.includes(permission);
    });
}

ipcMain.handle('toggle-global-notifications', (event, enabled) => {
    globalNotificationsEnabled = enabled;
    return globalNotificationsEnabled;
});

ipcMain.handle('cancel-download', (event, id) => {
    const entry = activeDownloads.get(id);
    if (entry && entry.item) {
        try {
            entry.item.cancel();
            return true;
        } catch (e) {
            return false;
        }
    }
    return false;
});

function getDesktopSearchDirectories() {
    const home = process.env.HOME || '';
    const xdgDataHome = process.env.XDG_DATA_HOME || path.join(home, '.local', 'share');
    const xdgDataDirs = (process.env.XDG_DATA_DIRS || '/usr/local/share:/usr/share').split(':');
    return [
        path.join(xdgDataHome, 'applications'),
        ...xdgDataDirs.map(d => path.join(d, 'applications'))
    ];
}

function findDesktopFile(desktopId) {
    if (!desktopId) return null;
    const dirs = getDesktopSearchDirectories();
    for (const dir of dirs) {
        const candidate = path.join(dir, desktopId);
        if (fs.existsSync(candidate)) return candidate;
    }
    return null;
}

function parseDesktopExec(execLine, targetPath) {
    const rawTokens = execLine.trim().match(/(?:[^\s"]+|"[^"]*")+/g) || [];
    const tokens = rawTokens.map(t => t.replace(/^"(.*)"$/, '$1'));
    let hasFieldCode = false;
    const args = [];
    for (const token of tokens) {
        if (token === '%f' || token === '%F' || token === '%u' || token === '%U') {
            args.push(targetPath);
            hasFieldCode = true;
        } else if (token.startsWith('%')) {
            continue;
        } else {
            args.push(token);
        }
    }
    if (!hasFieldCode && args.length > 0) {
        const bin = path.basename(args[0]);
        if (bin === 'nautilus' || bin === 'dolphin') {
            args.push(['-', '-', 'select'].join(''));
        }
        args.push(targetPath);
    }
    return args;
}

function hasDbusFileManager() {
    try {
        const res = spawnSync('busctl', ['status', 'org.freedesktop.FileManager1'], {
            timeout: 500,
            stdio: 'ignore'
        });
        return res.status === 0;
    } catch (e) {
        return false;
    }
}

function revealInFileManager(filePath) {
    if (!filePath || typeof filePath !== 'string') return false;
    if (!fs.existsSync(filePath)) return false;

    let targetPath = filePath;
    try {
        targetPath = fs.realpathSync(filePath);
    } catch (e) {}

    if (hasDbusFileManager()) {
        try {
            shell.showItemInFolder(targetPath);
            return true;
        } catch (e) {}
    }

    try {
        const xdgOutput = execSync('xdg-mime query default inode/directory', {
            encoding: 'utf8',
            timeout: 1000
        }).trim().split('\n')[0].trim();

        if (xdgOutput) {
            const desktopFile = findDesktopFile(xdgOutput);
            if (desktopFile) {
                const content = fs.readFileSync(desktopFile, 'utf8');
                const execMatch = content.match(/^Exec=(.*)$/m);
                if (execMatch && execMatch[1]) {
                    const cmdArgs = parseDesktopExec(execMatch[1], targetPath);
                    if (cmdArgs.length > 0) {
                        const child = spawn(cmdArgs[0], cmdArgs.slice(1), {
                            detached: true,
                            stdio: 'ignore'
                        });
                        child.unref();
                        return true;
                    }
                }
            }
        }
    } catch (e) {}

    try {
        shell.showItemInFolder(targetPath);
        return true;
    } catch (e) {
        return false;
    }
}

ipcMain.handle('open-download', async (event, filePath) => {
    if (!filePath || typeof filePath !== 'string') return false;
    try {
        if (!fs.existsSync(filePath)) {
            sendSystemNotification('Mise Download', 'File not found at destination path.');
            return false;
        }
        let targetPath = filePath;
        try { targetPath = fs.realpathSync(filePath); } catch (e) {}
        const err = await shell.openPath(targetPath);
        return !err;
    } catch (e) {
        return false;
    }
});

ipcMain.handle('reveal-download', async (event, filePath) => {
    if (!filePath || typeof filePath !== 'string') return false;
    try {
        if (!fs.existsSync(filePath)) {
            sendSystemNotification('Mise Download', 'File not found at destination path.');
            return false;
        }
        return revealInFileManager(filePath);
    } catch (e) {
        return false;
    }
});

ipcMain.handle('get-active-downloads', () => {
    const active = [];
    for (const [id, entry] of activeDownloads.entries()) {
        active.push({
            id: entry.id,
            filename: entry.filename,
            totalBytes: entry.totalBytes,
            receivedBytes: entry.receivedBytes,
            state: entry.state,
            savePath: entry.savePath
        });
    }
    return { active, recent: recentDownloads };
});

ipcMain.handle('get-app-version', () => {
    return {
        version: app.getVersion(),
        electron: process.versions.electron,
        chrome: process.versions.chrome,
        node: process.versions.node
    };
});

function isNewerVersion(latest, current) {
    const cleanL = String(latest || '').replace(/^v/i, '').trim();
    const cleanC = String(current || '').replace(/^v/i, '').trim();
    const partsL = cleanL.split('.').map(n => parseInt(n, 10) || 0);
    const partsC = cleanC.split('.').map(n => parseInt(n, 10) || 0);
    const maxLen = Math.max(partsL.length, partsC.length);
    for (let i = 0; i < maxLen; i++) {
        const l = partsL[i] || 0;
        const c = partsC[i] || 0;
        if (l > c) return true;
        if (l < c) return false;
    }
    return false;
}

function fetchLatestRelease() {
    return new Promise((resolve, reject) => {
        const options = {
            hostname: 'api.github.com',
            path: '/repos/Rakosn1cek/Mise-browser/releases/latest',
            headers: {
                'User-Agent': `Mise-Browser/${app.getVersion()}`,
                'Accept': 'application/vnd.github.v3+json'
            },
            timeout: 10000
        };

        const req = https.get(options, (res) => {
            if (res.statusCode === 404) {
                return resolve(null);
            }
            if (res.statusCode !== 200) {
                return reject(new Error(`GitHub API returned status ${res.statusCode}`));
            }
            let data = '';
            res.on('data', chunk => { data += chunk; });
            res.on('end', () => {
                try {
                    const parsed = JSON.parse(data);
                    resolve(parsed);
                } catch (err) {
                    reject(err);
                }
            });
        });

        req.on('error', reject);
        req.on('timeout', () => {
            req.destroy();
            reject(new Error('Request timed out'));
        });
    });
}

async function checkAppUpdates(manual = false) {
    try {
        const release = await fetchLatestRelease();
        if (!release || !release.tag_name) {
            return { updateAvailable: false, currentVersion: app.getVersion(), checkedAt: Date.now(), manual };
        }
        const currentVersion = app.getVersion();
        const latestTag = release.tag_name;
        const updateAvailable = isNewerVersion(latestTag, currentVersion);
        return {
            updateAvailable,
            currentVersion,
            latestVersion: latestTag,
            releaseName: release.name || latestTag,
            releaseUrl: release.html_url || 'https://github.com/Rakosn1cek/Mise-browser/releases',
            publishedAt: release.published_at,
            releaseNotes: release.body || '',
            checkedAt: Date.now(),
            manual
        };
    } catch (err) {
        return {
            error: err.message,
            updateAvailable: false,
            currentVersion: app.getVersion(),
            checkedAt: Date.now(),
            manual
        };
    }
}

ipcMain.handle('check-for-updates', async (event, manual = false) => {
    return checkAppUpdates(manual);
});

ipcMain.handle('open-external', async (event, url) => {
    if (!url || typeof url !== 'string') return false;
    try {
        const parsed = new URL(url);
        if (parsed.protocol === 'http:' || parsed.protocol === 'https:') {
            await shell.openExternal(url);
            return true;
        }
    } catch (e) {}
    return false;
});

// IPC HANDLERS
ipcMain.handle('flush-session-store', async (event, context) => {
    const targetSession = getSessionForContext(context);
    try {
        if (targetSession && targetSession.cookies) {
            await targetSession.cookies.flushStore();
            return true;
        }
    } catch (err) {
        return false;
    }
    return false;
});

ipcMain.handle('clear-active-cache', async (event, context) => {
    const targetSession = getSessionForContext(context);
    try {
        await targetSession.clearCache();
        await targetSession.clearStorageData({
            storages: ['cachestorage', 'serviceworkers', 'shadercache']
        });
        sendSystemNotification('Mise Browser', 'Cache and storage cleared successfully.');
        return true;
    } catch (err) {
        sendSystemNotification('Mise Error', `Cache clear failed: ${err.message}`);
        return false;
    }
});

ipcMain.handle('clear-domain-cookies', async (event, data) => {
    const targetSession = getSessionForContext(data);
    try {
        const parsed = new URL(data.urlStr);
        const cookies = await targetSession.cookies.get({ domain: parsed.hostname });
        
        for (const cookie of cookies) {
            const protocol = cookie.secure ? 'https://' : 'http://';
            const domain = cookie.domain.replace(/^\./, '');
            await targetSession.cookies.remove(`${protocol}${domain}${cookie.path}`, cookie.name);
        }

        sendSystemNotification('Mise Browser', `Cookies cleared for ${parsed.hostname}`);
        return true;
    } catch (err) {
        sendSystemNotification('Mise Error', `Cookie wipe failed: ${err.message}`);
        return false;
    }
});

ipcMain.handle('get-workspace-partition', (event, name) => {
    return getWorkspacePartition(name);
});

ipcMain.handle('read-notes', async () => {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        if (!fs.existsSync(NOTES_PATH)) fs.writeFileSync(NOTES_PATH, '', 'utf-8');
        return fs.readFileSync(NOTES_PATH, 'utf-8');
    } catch (err) {
        return '';
    }
});

ipcMain.handle('save-notes', async (event, content) => {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        fs.writeFileSync(NOTES_PATH, content, 'utf-8');
        return true;
    } catch (err) {
        return false;
    }
});

ipcMain.on('toggle-active-devtools', () => {
    if (!mainWindow) return;
    mainWindow.webContents.send('master-shortcut', 'toggle-devtools');
});

ipcMain.on('set-native-theme', (event, mode) => {
    if (mode === 'dark' || mode === 'light' || mode === 'system') {
        nativeTheme.themeSource = mode;
        return;
    }
    const cfg = loadBrowserConfig();
    nativeTheme.themeSource = cfg.webview_theme || 'dark';
});

ipcMain.handle('get-browser-settings', async () => {
    return loadBrowserConfig();
});

ipcMain.handle('save-browser-settings', async (event, newCfg) => {
    saveBrowserConfig(newCfg);
    return true;
});

ipcMain.on('renderer-ready', () => {
    isRendererReady = true;
    flushPendingUrls();
});

ipcMain.on('get-webview-preload-path', (event) => { 
    event.returnValue = path.join(__dirname, 'webview-preload.js'); 
});

ipcMain.on('get-pdf-viewer-path', (event) => { 
    event.returnValue = 'file://' + path.join(__dirname, 'assets', 'pdfjs', 'viewer.html'); 
});

ipcMain.on('get-pdf-viewer-url', (event, targetUrl) => { 
    event.returnValue = security.mintPdfViewerUrl(targetUrl); 
});

ipcMain.on('get-whats-new-path', (event) => { 
    event.returnValue = 'file://' + path.join(__dirname, 'assets', 'whats-new.html'); 
});

ipcMain.handle('read-pdf-data', async (event, payload) => {
    const { token, url: sourceUrl } = payload || {};
    if (!sourceUrl || typeof sourceUrl !== 'string') {
        throw new Error('Invalid PDF URL specified.');
    }

    security.validatePdfAccess(token, sourceUrl, event.sender ? event.sender.getURL() : '');

    if (sourceUrl.startsWith('file://') || sourceUrl.startsWith('/')) {
        let filePath = sourceUrl;
        if (sourceUrl.startsWith('file://')) {
            try {
                filePath = new URL(sourceUrl).pathname;
            } catch (e) {
                filePath = sourceUrl.replace(/^file:\/\//, '');
            }
        }
        filePath = decodeURIComponent(filePath);
        if (!fs.existsSync(filePath)) {
            throw new Error(`Local file not found: ${filePath}`);
        }
        const data = await fs.promises.readFile(filePath);
        return data;
    }

    if (sourceUrl.startsWith('http://') || sourceUrl.startsWith('https://')) {
        const callerSession = event.sender ? event.sender.session : session.defaultSession;
        const netFetch = (callerSession && callerSession.net && typeof callerSession.net.fetch === 'function')
            ? callerSession.net.fetch.bind(callerSession.net)
            : net.fetch;

        const response = await netFetch(sourceUrl);
        if (!response.ok) {
            throw new Error(`Failed to fetch PDF: HTTP ${response.status} ${response.statusText}`);
        }
        const arrayBuffer = await response.arrayBuffer();
        return Buffer.from(arrayBuffer);
    }

    if (sourceUrl.startsWith('data:application/pdf;base64,')) {
        const base64Data = sourceUrl.split(',')[1];
        return Buffer.from(base64Data, 'base64');
    }

    throw new Error(`Unsupported protocol for PDF loading: ${sourceUrl}`);
});

ipcMain.handle('save-pdf-file', async (event, payload) => {
    try {
        const { token, options } = payload || {};
        const { url, suggestedName, data } = options || {};

        security.validatePdfAccess(token, url, event.sender ? event.sender.getURL() : '');

        let cleanName = suggestedName || 'document.pdf';
        if (!cleanName.toLowerCase().endsWith('.pdf')) cleanName += '.pdf';

        const defaultSavePath = path.join(app.getPath('downloads'), cleanName);

        const { canceled, filePath } = await dialog.showSaveDialog(mainWindow, {
            title: 'Save PDF Document',
            defaultPath: defaultSavePath,
            filters: [
                { name: 'PDF Documents', extensions: ['pdf'] },
                { name: 'All Files', extensions: ['*'] }
            ]
        });

        if (canceled || !filePath) {
            return { success: false, canceled: true };
        }

        if (data && (Buffer.isBuffer(data) || data instanceof Uint8Array || ArrayBuffer.isView(data))) {
            await fs.promises.writeFile(filePath, Buffer.from(data));
        } else if (url && (url.startsWith('file://') || url.startsWith('/'))) {
            let srcPath = url.startsWith('file://') ? new URL(url).pathname : url;
            srcPath = decodeURIComponent(srcPath);
            await fs.promises.copyFile(srcPath, filePath);
        } else if (url && (url.startsWith('http://') || url.startsWith('https://'))) {
            const callerSession = event.sender ? event.sender.session : session.defaultSession;
            const netFetch = (callerSession && callerSession.net && typeof callerSession.net.fetch === 'function')
                ? callerSession.net.fetch.bind(callerSession.net)
                : net.fetch;
            const resp = await netFetch(url);
            const arrayBuf = await resp.arrayBuffer();
            await fs.promises.writeFile(filePath, Buffer.from(arrayBuf));
        } else {
            throw new Error('No PDF payload or valid source URL provided.');
        }

        const stats = await fs.promises.stat(filePath);
        const record = {
            id: `dl_pdf_${Date.now()}`,
            filename: path.basename(filePath),
            totalBytes: stats.size,
            receivedBytes: stats.size,
            state: 'completed',
            savePath: filePath,
            url: url || ''
        };

        if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('download-done', record);
        }

        if (globalNotificationsEnabled) {
            sendSystemNotification('PDF Saved', `${path.basename(filePath)} saved successfully.`);
        }

        return { success: true, filePath };
    } catch (err) {
        console.error('Failed to save PDF document:', err);
        return { success: false, error: err.message };
    }
});

ipcMain.handle('read-hinter-code', async () => {
    try {
        const hinterPath = path.join(__dirname, 'hinter.js');
        if (fs.existsSync(hinterPath)) return fs.readFileSync(hinterPath, 'utf8');
    } catch (err) {}
    return '';
});

let cachedReaderDistillerCode = null;
ipcMain.handle('read-reader-code', async () => {
    if (cachedReaderDistillerCode) return cachedReaderDistillerCode;
    try {
        const distillerPath = path.join(__dirname, 'modules', 'readerDistiller.js');
        if (fs.existsSync(distillerPath)) {
            cachedReaderDistillerCode = fs.readFileSync(distillerPath, 'utf8');
            return cachedReaderDistillerCode;
        }
    } catch (err) {}
    return '';
});

ipcMain.handle('get-session-recovery-status', async () => {
    return { wasCrash: isCrashRecovery };
});

ipcMain.handle('get-session', async () => {
    try {
        if (fs.existsSync(sessionPath)) {
            const raw = fs.readFileSync(sessionPath, 'utf8');
            const parsed = JSON.parse(raw);
            if (parsed && typeof parsed.workspaces === 'object' && parsed.current_workspace) {
                return parsed;
            }
        }
    } catch (err) {
        diagnostics.logEvent('warn', 'session', 'Failed to parse primary session file, attempting recovery from backup', { error: err.message });
    }

    // Resilient fallback: attempt recovery from rolling backup journal
    try {
        if (fs.existsSync(sessionBackupPath)) {
            const rawBackup = fs.readFileSync(sessionBackupPath, 'utf8');
            const parsedBackup = JSON.parse(rawBackup);
            if (parsedBackup && typeof parsedBackup.workspaces === 'object' && parsedBackup.current_workspace) {
                diagnostics.logEvent('info', 'session', 'Successfully recovered session state from session.json.bak');
                return parsedBackup;
            }
        }
    } catch (backupErr) {
        diagnostics.logEvent('error', 'session', 'Failed to recover session from backup', { error: backupErr.message });
    }

    return { current_workspace: "Workspace 1", workspaces: { "Workspace 1": ["https://duckduckgo.com"] } };
});

ipcMain.handle('save-session', async (event, sessionData) => {
    try {
        if (!sessionData || typeof sessionData !== 'object') return false;
        const dir = path.dirname(sessionPath);
        if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

        const serialized = JSON.stringify(sessionData, null, 4);
        const tempPath = sessionPath + '.tmp';

        // Write to temporary file first
        fs.writeFileSync(tempPath, serialized, 'utf8');

        // Rotate existing valid session to backup before overwriting
        if (fs.existsSync(sessionPath)) {
            try {
                fs.copyFileSync(sessionPath, sessionBackupPath);
            } catch (copyErr) {}
        }

        // Atomically rename temporary file to destination
        fs.renameSync(tempPath, sessionPath);
        return true;
    } catch (err) {
        diagnostics.logEvent('error', 'session', 'Failed to save session atomically', { error: err.message });
        return false;
    }
});

ipcMain.handle('search-history', async (event, query) => {
    const history = getHistoryCache();
    if (!query || !query.trim()) return history;

    const lowerQuery = query.toLowerCase();
    return history.filter(item => 
        item.title.toLowerCase().includes(lowerQuery) || 
        item.url.toLowerCase().includes(lowerQuery)
    );
});

ipcMain.handle('purge-history', async () => {
    try {
        inMemoryHistory = [];
        if (historyFlushTimer) {
            clearTimeout(historyFlushTimer);
            historyFlushTimer = null;
        }
        if (fs.existsSync(historyPath)) {
            fs.writeFileSync(historyPath, JSON.stringify([], null, 4), 'utf8');
        }
        return true;
    } catch (err) {
        return false;
    }
});

ipcMain.on('toggle-menu-bar', () => {
    openSettingsMenu();
});

// Window management controls
ipcMain.on('window-minimize', () => {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.minimize();
    }
});

ipcMain.on('window-toggle-maximize', () => {
    if (mainWindow && !mainWindow.isDestroyed()) {
        if (mainWindow.isMaximized()) {
            mainWindow.unmaximize();
        } else {
            mainWindow.maximize();
        }
    }
});

ipcMain.on('window-close', () => {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.close();
    }
});

ipcMain.handle('window-is-maximized', () => {
    return (mainWindow && !mainWindow.isDestroyed()) ? mainWindow.isMaximized() : false;
});

ipcMain.handle('get-keybinds', async () => {
    return keybinds.getKeybinds();
});

ipcMain.handle('save-keybinds', async (event, binds) => {
    return keybinds.saveKeybinds(CONFIG_DIR, binds);
});

ipcMain.handle('get-action-metadata', async () => {
    return keybinds.getActionMetadata();
});

ipcMain.handle('reload-keybinds', async () => {
    return keybinds.initializeKeybinds(CONFIG_DIR);
});

ipcMain.handle('read-bookmarks', async () => {
    try {
        if (!fs.existsSync(BOOKMARKS_PATH)) fs.writeFileSync(BOOKMARKS_PATH, JSON.stringify([]), 'utf-8');
        return JSON.parse(fs.readFileSync(BOOKMARKS_PATH, 'utf-8'));
    } catch (err) { return []; }
});

ipcMain.handle('save-bookmarks', async (event, data) => {
    try {
        fs.writeFileSync(BOOKMARKS_PATH, JSON.stringify(data, null, 4), 'utf-8');
        return true;
    } catch (err) { return false; }
});

ipcMain.handle('read-quickmarks', async () => {
    try {
        if (!fs.existsSync(QUICKMARKS_PATH)) fs.writeFileSync(QUICKMARKS_PATH, JSON.stringify({}), 'utf-8');
        return JSON.parse(fs.readFileSync(QUICKMARKS_PATH, 'utf-8'));
    } catch (err) { return {}; }
});

ipcMain.handle('save-quickmarks', async (event, data) => {
    try {
        fs.writeFileSync(QUICKMARKS_PATH, JSON.stringify(data, null, 4), 'utf-8');
        return true;
    } catch (err) { return false; }
});

ipcMain.on('execute-terminal-command', (event, commandStr) => {
    if (!commandStr || !commandStr.trim()) return;

    clipboard.writeText(commandStr);

    const platform = process.platform;
    if (platform === 'linux') {
        const emulators = ["kitty", "alacritty", "foot", "st", "xterm"];
        exec("which " + emulators.join(" "), (err, stdout) => {
            let chosenTerm = "";
            if (stdout) {
                const paths = stdout.trim().split("\n");
                if (paths.length > 0) {
                    chosenTerm = paths[0].split("/").pop();
                }
            }
            if (!chosenTerm) {
                chosenTerm = "xdg-terminal-exec";
            }
            spawn(chosenTerm, [], { detached: true, stdio: 'ignore' }).unref();
        });
    } else if (platform === 'darwin') {
        spawn('open', ['-a', 'Terminal'], { detached: true, stdio: 'ignore' }).unref();
    } else if (platform === 'win32') {
        exec('where wt', (err) => {
            if (!err) {
                spawn('wt', [], { detached: true, stdio: 'ignore' }).unref();
            } else {
                spawn('cmd.exe', [], { 
                    detached: true, 
                    stdio: 'ignore',
                    windowsHide: false
                }).unref();
            }
        });
    }
});

ipcMain.handle('update-browser-settings', async (event, newCfg) => {
    try {
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        const existing = loadBrowserConfig();
        const merged = { ...existing, ...newCfg };
        await fs.promises.writeFile(CONFIG_PATH, JSON.stringify(merged, null, 4), 'utf-8');
        if (Array.isArray(merged.trusted_domains)) {
            security.setTrustedDomains(merged.trusted_domains);
        }
        if (merged.spellchecker_language !== undefined) {
            applySpellcheckerToAllSessions(merged.spellchecker_language);
        }
        if (merged.enable_diagnostics !== undefined) {
            diagnostics.setDiagnosticsEnabled(merged.enable_diagnostics);
        }
        return true;
    } catch (e) {
        return false;
    }
});

ipcMain.handle('get-system-diagnostics', async (event, sessionStats) => {
    return diagnostics.gatherSystemDiagnostics(sessionStats);
});

ipcMain.handle('clear-diagnostics-logs', async () => {
    diagnostics.clearLogs();
    return true;
});

ipcMain.handle('set-diagnostics-enabled', async (event, enabled) => {
    diagnostics.setDiagnosticsEnabled(enabled);
    try {
        const cfg = loadBrowserConfig();
        cfg.enable_diagnostics = !!enabled;
        if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
        await fs.promises.writeFile(CONFIG_PATH, JSON.stringify(cfg, null, 4), 'utf-8');
    } catch (e) {}
    return true;
});

ipcMain.handle('log-diagnostics-event', async (event, data) => {
    if (data && typeof data === 'object') {
        diagnostics.logEvent(data.level, data.category, data.message, data.details);
    }
    return true;
});

function initializeMemoryWatcher() {
    setInterval(() => {
        if (!mainWindow || mainWindow.isDestroyed()) return;
        const metrics = app.getAppMetrics();
        let shouldHibernate = false;
        metrics.forEach(metric => {
            // Electron returns residentSet in KiB; 200 * 1024 KiB = 200 MB
            if (metric.type === 'Renderer' && metric.memory.residentSet > 200 * 1024) {
                const wc = webContents.fromId(metric.webContentsId);
                if (wc && !wc.isFocused()) {
                    shouldHibernate = true;
                }
            }
        });
        if (shouldHibernate) {
            mainWindow.webContents.send('master-shortcut', 'hibernate-inactive-tabs');
            if (typeof global.gc === 'function') {
                setTimeout(() => { try { global.gc(); } catch (e) {} }, 1000);
            }
        }
    }, 15000);
}

ipcMain.handle('compact-memory', () => {
    if (typeof global.gc === 'function') {
        try {
            global.gc();
            return true;
        } catch (e) {}
    }
    return false;
});

function buildSettingsSubmenu(cfg) {
    return [
        {
            label: 'Disable GPU Hardware Acceleration',
            type: 'checkbox',
            checked: cfg.disable_gpu,
            click: (menuItem) => {
                cfg.disable_gpu = menuItem.checked;
                saveBrowserConfig(cfg);
            }
        },
        {
            label: 'Enable Background Throttling',
            type: 'checkbox',
            checked: cfg.background_throttling,
            click: (menuItem) => {
                cfg.background_throttling = menuItem.checked;
                saveBrowserConfig(cfg);
            }
        },
        { type: 'separator' },
        {
            label: 'Renderer Process Limit',
            submenu: [1, 2, 3, 4, 5].map(num => ({
                label: `Limit to ${num} process${num === 1 ? '' : 'es'}`,
                type: 'radio',
                checked: cfg.process_limit === num,
                click: () => {
                    cfg.process_limit = num;
                    saveBrowserConfig(cfg);
                }
            }))
        },
        { type: 'separator' },
        {
            label: 'Tab Hibernation (Sleep Timeout)',
            submenu: [
                { minutes: 5, label: '5 minutes' },
                { minutes: 15, label: '15 minutes (Default)' },
                { minutes: 30, label: '30 minutes' },
                { minutes: 60, label: '1 hour' },
                { minutes: 0, label: 'Never (Disabled)' }
            ].map(opt => ({
                label: opt.label,
                type: 'radio',
                checked: (cfg.tab_sleep_timeout_minutes ?? 15) === opt.minutes,
                click: () => {
                    cfg.tab_sleep_timeout_minutes = opt.minutes;
                    if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
                    fs.writeFileSync(CONFIG_PATH, JSON.stringify(cfg, null, 4), 'utf-8');
                }
            }))
        },
        { type: 'separator' },
        {
            label: 'Auto-Collapse Sidebar (36px Strip)',
            type: 'checkbox',
            checked: cfg.sidebar_auto_collapse !== false,
            click: (menuItem) => {
                cfg.sidebar_auto_collapse = menuItem.checked;
                if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
                fs.writeFileSync(CONFIG_PATH, JSON.stringify(cfg, null, 4), 'utf-8');
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('master-shortcut', 'toggle-sidebar-collapse-mode', menuItem.checked);
                }
            }
        },
        {
            label: 'Window Controls (Sidebar)',
            type: 'checkbox',
            checked: !!cfg.show_window_controls,
            click: (menuItem) => {
                cfg.show_window_controls = menuItem.checked;
                if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
                fs.writeFileSync(CONFIG_PATH, JSON.stringify(cfg, null, 4), 'utf-8');
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('master-shortcut', 'toggle-window-controls', menuItem.checked);
                }
            }
        },
        { type: 'separator' },
        {
            label: 'Open User Scripts Directory',
            click: () => {
                const sDir = path.join(CONFIG_DIR, 'scripts');
                if (!fs.existsSync(sDir)) fs.mkdirSync(sDir, { recursive: true });
                shell.openPath(sDir);
            }
        },
        {
            label: 'Open User Styles Directory',
            click: () => {
                const stDir = path.join(CONFIG_DIR, 'styles');
                if (!fs.existsSync(stDir)) fs.mkdirSync(stDir, { recursive: true });
                shell.openPath(stDir);
            }
        },
        {
            label: 'Open Full Preferences Overlay',
            accelerator: 'Ctrl+H',
            click: () => {
                if (mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('master-shortcut', 'toggle-help');
                }
            }
        },
        {
            label: 'Restart Browser Now',
            click: () => { app.relaunch(); app.exit(0); }
        }
    ];
}

function openSettingsMenu() {
    if (!mainWindow || mainWindow.isDestroyed()) return;
    const cfg = loadBrowserConfig();
    const menu = Menu.buildFromTemplate(buildSettingsSubmenu(cfg));
    menu.popup({ window: mainWindow, x: 14, y: 14 });
}

function handleAppAction(action, sourceWebContents) {
    if (!mainWindow || mainWindow.isDestroyed()) return;

    if (action === 'toggle-menu-bar') {
        openSettingsMenu();
        return;
    }

    if (action === 'reload-active-tab') {
        if (sourceWebContents && sourceWebContents !== mainWindow.webContents) {
            sourceWebContents.reload();
        } else {
            mainWindow.webContents.send('master-shortcut', 'reload-active-tab');
        }
        return;
    }

    if (action === 'toggle-private-mode') {
        privateBrowsingEnabled = !privateBrowsingEnabled;
        mainWindow.webContents.send('master-shortcut', 'toggle-private-mode', privateBrowsingEnabled);
        return;
    }

    mainWindow.webContents.send('master-shortcut', action);
}

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1600,
        height: 1040,
        icon: path.join(__dirname, 'assets', 'icons', 'Mise-logo256.png'),
        frame: true,
        autoHideMenuBar: false,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
            webviewTag: true
        }
    });
    
    const cfg = loadBrowserConfig();
    nativeTheme.themeSource = cfg.webview_theme || 'dark';
    const systemMenu = Menu.buildFromTemplate([
        {
            label: 'Mise Settings',
            submenu: buildSettingsSubmenu(cfg)
        }
    ]);
    mainWindow.setMenu(systemMenu);
    mainWindow.setMenuBarVisibility(false);
    
    // Configure default and volatile private sessions
    configureAndHardenSession(session.defaultSession);
    configureAndHardenSession(session.fromPartition('MisePrivateProfile'));

    // Configure active workspace partition; other partitions configure lazily on demand
    try {
        let initialSession = null;
        if (fs.existsSync(sessionPath)) {
            try { initialSession = JSON.parse(fs.readFileSync(sessionPath, 'utf8')); } catch (e) {}
        }
        if (!initialSession && fs.existsSync(sessionBackupPath)) {
            try { initialSession = JSON.parse(fs.readFileSync(sessionBackupPath, 'utf8')); } catch (e) {}
        }
        const activeWs = (initialSession && initialSession.current_workspace) || 'default';
        configureAndHardenSession(session.fromPartition(getWorkspacePartition(activeWs)));
    } catch (err) {
        console.error('Failed to initialise active workspace session:', err);
    }

    security.setTrustedDomains(loadBrowserConfig().trusted_domains || []);

    mainWindow.loadFile('index.html');

    mainWindow.on('close', () => {
        markSentinelState(true);
    });

    mainWindow.on('maximize', () => {
        if (mainWindow && !mainWindow.isDestroyed() && mainWindow.webContents) {
            mainWindow.webContents.send('window-maximized-state', true);
        }
    });

    mainWindow.on('unmaximize', () => {
        if (mainWindow && !mainWindow.isDestroyed() && mainWindow.webContents) {
            mainWindow.webContents.send('window-maximized-state', false);
        }
    });

    mainWindow.webContents.once('did-finish-load', () => {
        setTimeout(() => {
            if (!isRendererReady) {
                isRendererReady = true;
                flushPendingUrls();
            }
            // Trigger idle heap sweep to release startup deserialisation buffers
            if (typeof global.gc === 'function') {
                try { global.gc(); } catch (e) {}
            }
        }, 1200);

        // Check for updates in background after startup
        setTimeout(async () => {
            try {
                const updateInfo = await checkAppUpdates(false);
                if (updateInfo && updateInfo.updateAvailable && mainWindow && !mainWindow.isDestroyed()) {
                    mainWindow.webContents.send('update-available', updateInfo);
                }
            } catch (e) {}
        }, 6000);
    });

    ipcMain.on('is-trusted-domain', (event, hostname) => {
        event.returnValue = security.isTrustedDomain(hostname);
    });

    ipcMain.on('get-google-signin-user-agent', (event, hostname) => {
        event.returnValue = security.getGoogleSignInUserAgent(hostname);
    });

    ipcMain.handle('clear-google-auth-cookies', async (event, partitionName) => {
        const targetSession = partitionName ? session.fromPartition(partitionName) : session.fromPartition('MisePrivateProfile');
        await security.clearGoogleAuthCookies(targetSession);
        return true;
    });

    userContent.initializeUserContent(CONFIG_DIR, mainWindow);

    mainWindow.webContents.on('will-attach-webview', (event, webPreferences, params) => {
        security.hardenWebviewPreferences(webPreferences);
        const partitionStr = (webPreferences && webPreferences.partition) || (params && params.partition);
        if (partitionStr) {
            configureAndHardenSession(session.fromPartition(partitionStr));
        }
    });

    ipcMain.on('bubble-webview-key', (event, action) => {
        if (mainWindow && mainWindow.webContents) {
            mainWindow.webContents.send('master-shortcut', action);
        }
    });

    mainWindow.webContents.on('before-input-event', (event, input) => {
        if (input.type !== 'keyDown') return;

        const action = keybinds.getActionForInput(input);
        if (action) {
            event.preventDefault();
            handleAppAction(action, mainWindow.webContents);
        }
    });
}

app.on('web-contents-created', (event, webContents) => {
    if (webContents.session) {
        configureAndHardenSession(webContents.session);
    }

    if (webContents.getType() === 'webview') {
        webContents.setMaxListeners(30);

        webContents.on('did-navigate', (navEvent, url) => {
            logVisit(webContents.getTitle(), url);
            if (url) {
                try {
                    const parsedUrl = new URL(url);
                    if ((parsedUrl.hostname === 'accounts.google.com' || parsedUrl.hostname.endsWith('.accounts.google.com')) &&
                        parsedUrl.pathname.includes('signin/rejected')) {
                        security.clearGoogleAuthCookies(webContents.session);
                    }
                } catch (e) {
                    // Ignore URL parsing errors
                }
            }
        });

        webContents.on('did-navigate-in-page', (navEvent, url) => {
            logVisit(webContents.getTitle(), url);
        });

        webContents.on('context-menu', (contextEvent, params) => {
            contextEvent.preventDefault();
            
            const menu = new Menu();

            const openShareModal = (url) => {
                if (mainWindow && mainWindow.webContents) {
                    mainWindow.webContents.send('master-shortcut', 'open-transient-share', url);
                }
            };

            const openLinkTab = (url) => {
                if (mainWindow && mainWindow.webContents) {
                    mainWindow.webContents.send('master-shortcut', 'spawn-tab-with-url', url);
                }
            };

            const openLinkInSplit = (url) => {
                if (mainWindow && mainWindow.webContents) {
                    mainWindow.webContents.send('master-shortcut', 'open-link-in-split', url);
                }
            };

            const targetUrl = params.linkURL || params.srcURL || params.pageURL || webContents.getURL();
            const targetText = params.selectionText ? params.selectionText.trim() : webContents.getTitle();
            
            const encodedUrl = encodeURIComponent(targetUrl || '');
            const encodedText = encodeURIComponent(targetText || '');
        
            if (params.dictionarySuggestions && params.dictionarySuggestions.length > 0) {
                params.dictionarySuggestions.forEach(suggestion => {
                    menu.append(new MenuItem({
                        label: suggestion,
                        click: () => webContents.replaceMisspelling(suggestion)
                    }));
                });
                menu.append(new MenuItem({ type: 'separator' }));
            }
        
            if (params.selectionText && params.selectionText.trim() !== '') {
                menu.append(new MenuItem({ label: 'Copy', role: 'copy' }));
            }
            
            if (params.linkURL && params.linkURL.trim() !== '') {
                menu.append(new MenuItem({
                    label: 'Open Link in New Tab',
                    click: () => openLinkTab(params.linkURL)
                }));
                menu.append(new MenuItem({
                    label: 'Open Link in Split View',
                    click: () => openLinkInSplit(params.linkURL)
                }));
                menu.append(new MenuItem({
                    label: 'Copy Link Address',
                    click: () => {
                        clipboard.writeText(params.linkURL);
                    }
                }));
            }
        
            if (params.isEditable) {
                menu.append(new MenuItem({ label: 'Paste', role: 'paste' }));
                menu.append(new MenuItem({ label: 'Cut', role: 'cut' }));
                menu.append(new MenuItem({ label: 'Select All', role: 'selectall' }));
            }
        
            if (params.mediaType === 'image') {
                menu.append(new MenuItem({
                    label: 'Save Image As...',
                    click: () => { if (mainWindow) mainWindow.webContents.downloadURL(params.srcURL); }
                }));
                menu.append(new MenuItem({
                    label: 'Copy Image Address',
                    click: () => { 
                        clipboard.writeText(params.srcURL); 
                    }
                }));
            }
        
            menu.append(new MenuItem({ type: 'separator' }));
        
            const shareSubmenu = new Menu();
            shareSubmenu.append(new MenuItem({
                label: 'Share to WhatsApp',
                click: () => openShareModal(`https://web.whatsapp.com/send?text=${encodedText}%20${encodedUrl}`)
            }));
            shareSubmenu.append(new MenuItem({
                label: 'Share to X (Twitter)',
                click: () => openShareModal(`https://twitter.com/intent/tweet?text=${encodedText}&url=${encodedUrl}`)
            }));
            shareSubmenu.append(new MenuItem({
                label: 'Share to Telegram',
                click: () => openShareModal(`https://t.me/share/url?url=${encodedUrl}&text=${encodedText}`)
            }));
            shareSubmenu.append(new MenuItem({
                label: 'Share to Reddit',
                click: () => openShareModal(`https://www.reddit.com/submit?url=${encodedUrl}&title=${encodedText}`)
            }));
        
            shareSubmenu.append(new MenuItem({
                label: 'Share via Email',
                click: () => {
                    const cfg = loadBrowserConfig();
                    const handler = cfg.email_handler || 'system';
        
                    const pageTitle = webContents.getTitle() || 'Shared link';
                    const currentUrl = params.pageURL || webContents.getURL() || '';
                    const selectedSnippet = params.selectionText ? params.selectionText.trim() : '';
                    const imageSrc = params.srcURL || '';
        
                    let emailSubject = pageTitle;
                    let emailBody = '';
        
                    if (selectedSnippet) {
                        emailSubject = `Snippet from: ${pageTitle}`;
                        emailBody = `${selectedSnippet}\n\nSource: ${currentUrl}`;
                        clipboard.writeText(selectedSnippet);
                    } else if (imageSrc) {
                        emailSubject = `Image from: ${pageTitle}`;
                        emailBody = `${imageSrc}\n\nPage: ${currentUrl}`;
                    } else {
                        emailSubject = pageTitle;
                        emailBody = currentUrl;
                    }
        
                    const encSubject = encodeURIComponent(emailSubject);
                    const encBody = encodeURIComponent(emailBody);
        
                    if (handler === 'system') {
                        const mailto = `mailto:?subject=${encSubject}&body=${encBody}`;
                        shell.openExternal(mailto).catch(() => {
                            openShareModal(`https://mail.google.com/mail/?view=cm&fs=1&su=${encSubject}&body=${encBody}`);
                        });
                    } else {
                        let composeUrl = handler;
                        if (composeUrl.includes('%s')) {
                            composeUrl = composeUrl.replace('%s', encSubject);
                        }
                        if (composeUrl.includes('%b')) {
                            composeUrl = composeUrl.replace('%b', encBody);
                        }
                        openShareModal(composeUrl);
                    }
                }
            }));
        
            shareSubmenu.append(new MenuItem({ type: 'separator' }));
            shareSubmenu.append(new MenuItem({
                label: 'Copy Markdown Link',
                click: () => {
                    const md = `[${targetText || targetUrl}](${targetUrl})`;
                    clipboard.writeText(md);
                }
            }));
        
            menu.append(new MenuItem({
                label: 'Share...',
                submenu: shareSubmenu
            }));
        
            if (menu.items.length <= 2) {
                menu.append(new MenuItem({ label: 'Back', click: () => { webContents.send('master-shortcut', 'go-back-signal'); } }));
                menu.append(new MenuItem({ label: 'Forward', click: () => { webContents.send('master-shortcut', 'go-forward-signal'); } }));
                menu.append(new MenuItem({ label: 'Reload', click: () => { webContents.reload(); } }));
            }
        
            menu.popup({ window: mainWindow });
        });

        webContents.setWindowOpenHandler((details) => {
            if (details.url && details.url !== 'about:blank') {
                if (mainWindow && mainWindow.webContents) {
                    mainWindow.webContents.send('master-shortcut', 'spawn-tab-with-url', details.url);
                }
            }
            return { action: 'deny' };
        });

        webContents.on('before-input-event', (inputEvent, input) => {
            if (input.type !== 'keyDown') return;
            if (!mainWindow || !mainWindow.webContents) return;

            const action = keybinds.getActionForInput(input);
            if (action) {
                inputEvent.preventDefault();
                handleAppAction(action, webContents);
            }
        });

        webContents.on('render-process-gone', (event, details) => {
            diagnostics.logEvent('error', 'webview', `Webview render process terminated: reason=${details.reason} exitCode=${details.exitCode}`);
        });

        webContents.on('unresponsive', () => {
            diagnostics.logEvent('warn', 'performance', 'Webview became unresponsive');
        });

        webContents.on('responsive', () => {
            diagnostics.logEvent('info', 'performance', 'Webview regained responsiveness');
        });

        webContents.on('did-fail-load', (event, errorCode, errorDescription, validatedURL) => {
            if (errorCode === -3) return;
            diagnostics.logEvent('warn', 'network', `Navigation failed: code=${errorCode} description=${errorDescription}`, { url: validatedURL });
        });
    }
});

app.on('render-process-gone', (event, wc, details) => {
    diagnostics.logEvent('error', 'process', `Render process gone: reason=${details.reason} exitCode=${details.exitCode}`);
});

app.on('child-process-gone', (event, details) => {
    diagnostics.logEvent('warn', 'process', `Child process gone: type=${details.type} reason=${details.reason} exitCode=${details.exitCode}`);
});

if (gotSingleInstanceLock) {
    app.on('second-instance', (event, commandLine) => {
        if (mainWindow) {
            if (mainWindow.isMinimized()) mainWindow.restore();
            mainWindow.show();
            mainWindow.focus();

            const incomingUrls = extractUrlsFromArgs(commandLine);
            incomingUrls.forEach(url => dispatchTabUrl(url));
        }
    });

    app.on('open-url', (event, url) => {
        event.preventDefault();
        if (mainWindow) {
            if (mainWindow.isMinimized()) mainWindow.restore();
            mainWindow.show();
            mainWindow.focus();
        }
        dispatchTabUrl(url);
    });

    app.on('before-quit', () => {
        markSentinelState(true);
        flushHistoryToDisk();
    });

    app.whenReady().then(() => {
        checkAndInitializeSentinel();
        diagnostics.initDiagnostics(loadBrowserConfig());
        if (isCrashRecovery) {
            diagnostics.logEvent('warn', 'session', 'Detected previous unclean shutdown or crash, initialising recovery mode');
        }
        createWindow();
        initializeMemoryWatcher();
    });
}

