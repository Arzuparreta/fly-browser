// Settings are a browser-owned tab, never a URL loaded by a guest webview.
export const SETTINGS_URL = 'fly://settings';
export const isSettingsTab = url => typeof url === 'string' && (url === SETTINGS_URL || url.startsWith(`${SETTINGS_URL}#`));
