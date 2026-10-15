// preload.js
const { contextBridge, ipcRenderer } = require('electron');

// Preload private API exposure
contextBridge.exposeInMainWorld('flyAPI', {
    getSession: () => ipcRenderer.invoke('get-session'),
    getSessionRecoveryStatus: () => ipcRenderer.invoke('get-session-recovery-status'),
    saveSession: (sessionData) => ipcRenderer.invoke('save-session', sessionData),
    getWebviewPreloadPath: () => ipcRenderer.sendSync('get-webview-preload-path'),
    getPdfViewerPath: () => ipcRenderer.sendSync('get-pdf-viewer-path'),
    getPdfViewerUrl: (targetUrl) => ipcRenderer.sendSync('get-pdf-viewer-url', targetUrl),
    getWhatsNewPath: () => ipcRenderer.sendSync('get-whats-new-path'),
    readHinterCode: () => ipcRenderer.invoke('read-hinter-code'),
    readReaderCode: () => ipcRenderer.invoke('read-reader-code'),
    showContextMenu: (params) => ipcRenderer.send('show-context-menu', params),
    getBrowserSettings: () => ipcRenderer.invoke('get-browser-settings'),
    saveBrowserSettings: (cfg) => ipcRenderer.invoke('save-browser-settings', cfg),
    updateBrowserSettings: (cfg) => ipcRenderer.invoke('update-browser-settings', cfg),
    toggleGlobalNotifications: (enabled) => ipcRenderer.invoke('toggle-global-notifications', enabled),
    getGlobalNotificationsState: () => ipcRenderer.invoke('get-global-notifications-state'),
    clearDomainCookies: (data) => ipcRenderer.invoke('clear-domain-cookies', data),
    clearActiveCache: (options) => ipcRenderer.invoke('clear-active-cache', options),
    executeTerminalCommand: (commandStr) => ipcRenderer.send('execute-terminal-command', commandStr),
    searchHistory: (query) => ipcRenderer.invoke('search-history', query),
    purgeHistory: () => ipcRenderer.invoke('purge-history'),
    setNativeTheme: (mode) => ipcRenderer.send('set-native-theme', mode),
    readBookmarks: () => ipcRenderer.invoke('read-bookmarks'),
    saveBookmarks: (data) => ipcRenderer.invoke('save-bookmarks', data),
    readQuickmarks: () => ipcRenderer.invoke('read-quickmarks'),
    saveQuickmarks: (data) => ipcRenderer.invoke('save-quickmarks', data),
    onMasterShortcut: (callback) => {
        ipcRenderer.on('master-shortcut', (event, action, ...args) => callback(action, ...args));
    },
    signalRendererReady: () => ipcRenderer.send('renderer-ready'),
    toggleMenuBar: () => ipcRenderer.send('toggle-menu-bar'),
    executeKeyboardAction: action => ipcRenderer.send('keyboard-action', action),
    getKeyboardConfig: () => ipcRenderer.invoke('get-keyboard-config'),
    getKeyboardDefaults: () => ipcRenderer.invoke('get-keyboard-defaults'),
    onKeyboardConfigChanged: callback => {
        const handler = (_event, config) => callback(config);
        ipcRenderer.on('keyboard-config-changed', handler);
        return () => ipcRenderer.removeListener('keyboard-config-changed', handler);
    },
    getKeybinds: () => ipcRenderer.invoke('get-keybinds'),
    saveKeybinds: (binds) => ipcRenderer.invoke('save-keybinds', binds),
    resolveKeybinds: (binds) => ipcRenderer.invoke('resolve-keybinds', binds),
    getActionMetadata: () => ipcRenderer.invoke('get-action-metadata'),
    reloadKeybinds: () => ipcRenderer.invoke('reload-keybinds'),
    getWorkspacePartition: (name) => ipcRenderer.invoke('get-workspace-partition', name),
    allocateWorkspacePartition: (name) => ipcRenderer.invoke('allocate-workspace-partition', name),
    flushSessionStore: (context) => ipcRenderer.invoke('flush-session-store', context),
    onDownloadStarted: (callback) => {
        const handler = (event, data) => callback(data);
        ipcRenderer.on('download-started', handler);
        return () => ipcRenderer.removeListener('download-started', handler);
    },
    onDownloadProgress: (callback) => {
        const handler = (event, data) => callback(data);
        ipcRenderer.on('download-progress', handler);
        return () => ipcRenderer.removeListener('download-progress', handler);
    },
    onDownloadDone: (callback) => {
        const handler = (event, data) => callback(data);
        ipcRenderer.on('download-done', handler);
        return () => ipcRenderer.removeListener('download-done', handler);
    },
    cancelDownload: (id) => ipcRenderer.invoke('cancel-download', id),
    openDownload: (filePath) => ipcRenderer.invoke('open-download', filePath),
    revealDownload: (filePath) => ipcRenderer.invoke('reveal-download', filePath),
    getActiveDownloads: () => ipcRenderer.invoke('get-active-downloads'),
    getAppVersion: () => ipcRenderer.invoke('get-app-version'),
    getUserContentForUrl: (url) => ipcRenderer.invoke('get-user-content-for-url', url),
    reloadUserContent: () => ipcRenderer.invoke('reload-user-content'),
    openUserScriptsDir: () => ipcRenderer.invoke('open-user-scripts-dir'),
    openUserStylesDir: () => ipcRenderer.invoke('open-user-styles-dir'),
    openUserScriptStorageDir: () => ipcRenderer.invoke('open-user-script-storage-dir'),
    getUserContentSummary: () => ipcRenderer.invoke('get-user-content-summary'),
    compactMemory: () => ipcRenderer.invoke('compact-memory'),
    checkForUpdates: (manual) => ipcRenderer.invoke('check-for-updates', manual),
    installUpdate: () => ipcRenderer.invoke('install-update'),
    onUpdateProgress: (callback) => {
        const handler = (event, percent) => callback(percent);
        ipcRenderer.on('update-progress', handler);
        return () => ipcRenderer.removeListener('update-progress', handler);
    },
    getDesktopIntegration: () => ipcRenderer.invoke('desktop-integration-status'),
    addToApplicationsMenu: () => ipcRenderer.invoke('desktop-integration-install'),
    removeFromApplicationsMenu: () => ipcRenderer.invoke('desktop-integration-remove'),
    makeDefaultBrowser: () => ipcRenderer.invoke('desktop-integration-make-default'),
    openExternal: (url) => ipcRenderer.invoke('open-external', url),
    onUpdateAvailable: (callback) => {
        const handler = (event, info) => callback(info);
        ipcRenderer.on('update-available', handler);
        return () => ipcRenderer.removeListener('update-available', handler);
    },
    onUserContentUpdated: (callback) => {
        const handler = (event, kind) => callback(kind);
        ipcRenderer.on('user-content-updated', handler);
        return () => ipcRenderer.removeListener('user-content-updated', handler);
    },
    getSystemDiagnostics: (sessionStats) => ipcRenderer.invoke('get-system-diagnostics', sessionStats),
    clearDiagnosticsLogs: () => ipcRenderer.invoke('clear-diagnostics-logs'),
    setDiagnosticsEnabled: (enabled) => ipcRenderer.invoke('set-diagnostics-enabled', enabled),
    logDiagnosticsEvent: (eventData) => ipcRenderer.invoke('log-diagnostics-event', eventData),
    clearGoogleAuthCookies: (partitionName) => ipcRenderer.invoke('clear-google-auth-cookies', partitionName),
    minimizeWindow: () => ipcRenderer.send('window-minimize'),
    toggleMaximizeWindow: () => ipcRenderer.send('window-toggle-maximize'),
    closeWindow: () => ipcRenderer.send('window-close'),
    isWindowMaximized: () => ipcRenderer.invoke('window-is-maximized'),
    onWindowMaximizedChange: (callback) => {
        const handler = (event, isMax) => callback(isMax);
        ipcRenderer.on('window-maximized-state', handler);
        return () => ipcRenderer.removeListener('window-maximized-state', handler);
    },
    // Opt-in Chrome extensions (extensions.js)
    extensionsEnsure: (workspace, partition) => ipcRenderer.invoke('extensions-ensure', workspace, partition),
    extensionsTabFocused: (webContentsId) => ipcRenderer.send('extensions-tab-focused', webContentsId),
    extensionsActivate: (extensionId, partition) => ipcRenderer.invoke('extensions-activate', extensionId, partition),
    extensionsRunCommand: (extensionId, partition, name) => ipcRenderer.invoke('extensions-run-command', extensionId, partition, name),
    extensionsGetActions: (partition) => ipcRenderer.invoke('extensions-get-actions', partition),
    extensionsGetState: (options) => ipcRenderer.invoke('extensions-get-state', options),
    extensionsApplyConfig: () => ipcRenderer.invoke('extensions-apply-config'),
    extensionsRemove: (folder) => ipcRenderer.invoke('extensions-remove', folder),
    extensionsReload: () => ipcRenderer.invoke('extensions-reload'),
    extensionsOpenDir: () => ipcRenderer.invoke('extensions-open-dir')
});

