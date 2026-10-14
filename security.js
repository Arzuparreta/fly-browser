// security.js
// Decoupled security configuration module for the Fly browser

const { ElectronBlocker } = require('@ghostery/adblocker-electron');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { app, ipcMain } = require('electron');

let blockerInstance = null;
let blockerInitialisation = null;
let ipcInitialized = false;
const networkExtensionSessions = new WeakSet();

// Domains the user has explicitly marked as trusted (e.g. their bank, their
// usual checkout sites). Anti-fingerprinting and ad/tracker blocking are
// relaxed ONLY for these domains so the sites' fraud-detection checks pass.
// Every other site keeps full protection.
let trustedDomains = [];
let appliedExceptionFilters = [];

// Define a permanent location for the compiled adblocker cache file
const userDataDir = (app && typeof app.getPath === 'function')
    ? app.getPath('userData')
    : path.join(require('os').tmpdir(), 'fly-browser');
const CACHE_PATH = path.join(userDataDir, 'adblock_cache.bin');

// Infrastructure domains required for Google authentication and bot detection
const GOOGLE_AUTH_DOMAINS = [
    'accounts.google.com',
    'myaccount.google.com',
    'google.com',
    'google.co.uk',
    'gstatic.com',
    'googleapis.com',
    'googleusercontent.com',
    'gvt1.com',
    'recaptcha.net',
    'youtube.com'
];

// Google's sign-in serves Electron a lighter flow that it accepts, but sends the
// standardised Chrome identity into full Chrome checks that an embedded runtime
// fails ("This browser or app may not be secure"). Sign-in pages therefore get
// Electron's own User-Agent instead.
const GOOGLE_SIGNIN_HOST = 'accounts.google.com';
let googleSignInUserAgent = '';

// Called by main.js with Electron's default User-Agent, before it is replaced
function setGoogleSignInUserAgent(defaultUserAgent) {
    // Drop the app name token, keeping the Chrome and Electron versions
    googleSignInUserAgent = String(defaultUserAgent || '')
        .replace(/\(KHTML, like Gecko\) .*?Chrome\//, '(KHTML, like Gecko) Chrome/');
}

function getGoogleSignInUserAgent(hostname) {
    return hostname === GOOGLE_SIGNIN_HOST ? googleSignInUserAgent : '';
}

function isGoogleSignInRequest(details) {
    const hostOf = (url) => { try { return new URL(url).hostname; } catch (e) { return ''; } };
    // Navigations are judged by destination; the page URL is still the previous one here
    if (details.resourceType === 'mainFrame') return hostOf(details.url) === GOOGLE_SIGNIN_HOST;
    // Everything a sign-in page loads carries the same identity
    try {
        return !!details.webContents && hostOf(details.webContents.getURL()) === GOOGLE_SIGNIN_HOST;
    } catch (e) {
        return false;
    }
}

function isTrustedDomain(hostname) {
    if (!hostname) return false;
    const host = String(hostname).toLowerCase().split(':')[0];

    // Core authentication endpoints are always trusted to ensure uninterrupted account login
    if (GOOGLE_AUTH_DOMAINS.some(gd => host === gd || host.endsWith('.' + gd))) {
        return true;
    }

    return trustedDomains.some(entry => {
        const domain = String(entry || '').toLowerCase().trim();
        if (!domain) return false;
        return host === domain || host.endsWith('.' + domain);
    });
}

function buildExceptionFilters(domains) {
    const filters = [];
    const allDomains = new Set();

    domains.forEach(entry => {
        const domain = String(entry || '').toLowerCase().trim();
        if (domain) allDomains.add(domain);
    });

    allDomains.forEach(domain => {
        // Whitelist all sub-resources, scripts, beacons, and websocket frames
        filters.push(`@@||${domain}^$important`);
        filters.push(`@@||${domain}^`);
        // Disable cosmetic CSS/DOM element hiding
        filters.push(`${domain}#@#*`);
    });

    return filters;
}

function applySpellcheckerLanguage(targetSession, languageCode) {
    if (!targetSession) return;
    try {
        const lang = String(languageCode || '').trim();
        if (!lang || lang === 'disabled' || lang === 'none') {
            if (typeof targetSession.setSpellCheckerEnabled === 'function') {
                targetSession.setSpellCheckerEnabled(false);
            }
            if (typeof targetSession.setSpellCheckerLanguages === 'function') {
                targetSession.setSpellCheckerLanguages([]);
            }
        } else {
            if (typeof targetSession.setSpellCheckerEnabled === 'function') {
                targetSession.setSpellCheckerEnabled(true);
            }
            if (typeof targetSession.setSpellCheckerLanguages === 'function') {
                targetSession.setSpellCheckerLanguages([lang]);
            }
        }
    } catch (err) {
        console.error('Failed to configure spellchecker language:', err);
    }
}

function hardenSession(targetSession, spellLang = 'en-GB') {
    applySpellcheckerLanguage(targetSession, spellLang);
    initialiseAdblocker(targetSession);

    // Prevent WebRTC from probing non-default local interfaces
    if (typeof targetSession.setWebRTCIPHandlingPolicy === 'function') {
        targetSession.setWebRTCIPHandlingPolicy('default_public_interface_only');
    }

    // Kept restricted permissions, but removed 'notifications' so the main toggle handles it
    const blockedPermissions = ['media', 'geolocation', 'midiSysex', 'audio', 'video'];

    targetSession.setPermissionRequestHandler((webContents, permission, callback) => {
        let hostname = '';
        try { hostname = new URL(webContents.getURL()).hostname; } catch (e) {}

        if (isTrustedDomain(hostname)) {
            return callback(true);
        }
        if (permission === 'clipboard-read') {
            return callback(false);
        }
        if (permission === 'clipboard-sanitized-write') {
            return callback(true);
        }
        if (permission === 'notifications') {
            // Respect the global toggle state if defined, or grant
            return callback(true);
        }
        if (blockedPermissions.includes(permission)) {
            return callback(false);
        }
        callback(true);
    });

    targetSession.setPermissionCheckHandler((webContents, permission, origin) => {
        let hostname = '';
        try { hostname = new URL(origin).hostname; } catch (e) {}

        if (isTrustedDomain(hostname)) {
            return true;
        }
        if (permission === 'clipboard-read') {
            return false;
        }
        if (permission === 'clipboard-sanitized-write') {
            return true;
        }
        if (permission === 'notifications') {
            return true;
        }
        if (blockedPermissions.includes(permission)) {
            return false;
        }
        return true;
    });

    attachNetworkHandlers(targetSession);
    attachHeaderHandler(targetSession);
}

function attachHeaderHandler(targetSession) {
    if (networkExtensionSessions.has(targetSession)) return;
    const cleanUserAgent = (ua) => {
        return (ua || '')
            .replace(/fly-browser\/[0-9.]+\s*/gi, '')
            .replace(/Electron\/[0-9.]+\s*/gi, '')
            .replace(/Chrome\/(\d+)\.[\d.]+/i, 'Chrome/$1.0.0.0')
            .trim();
    };

    if (targetSession.webRequest && typeof targetSession.webRequest.onBeforeSendHeaders === 'function') {
        targetSession.webRequest.onBeforeSendHeaders((details, callback) => {
            let hostname = '';
            try { hostname = new URL(details.url).hostname; } catch (e) {}

            if (googleSignInUserAgent && isGoogleSignInRequest(details)) {
                const headers = details.requestHeaders || {};
                headers['User-Agent'] = googleSignInUserAgent;
                return callback({ requestHeaders: headers });
            }

            // Keep unmodified headers on trusted authentication domains
            if (isTrustedDomain(hostname)) {
                return callback({ requestHeaders: details.requestHeaders });
            }

            const headers = details.requestHeaders;
            if (headers) {
                if (headers['User-Agent']) {
                    headers['User-Agent'] = cleanUserAgent(headers['User-Agent']);
                }
                const chromeMatch = (headers['User-Agent'] || '').match(/Chrome\/(\d+)\.([\d.]+)/);
                const realChromeMajor = (process.versions && process.versions.chrome) ? process.versions.chrome.split('.')[0] : '132';
                const chromeMajor = chromeMatch ? chromeMatch[1] : realChromeMajor;

                headers['sec-ch-ua'] = `"Chromium";v="${chromeMajor}", "Google Chrome";v="${chromeMajor}", "Not-A.Brand";v="99"`;
                headers['sec-ch-ua-full-version-list'] = `"Chromium";v="${chromeMajor}.0.0.0", "Google Chrome";v="${chromeMajor}.0.0.0", "Not-A.Brand";v="99.0.0.0"`;
                headers['Accept-Language'] = 'en-GB,en-US;q=0.9,en;q=0.8';
            }
            callback({ requestHeaders: headers });
        });
    }
}

// Electron selects either its native handlers or chrome.webRequest for an entire
// session. Even one native listener prevents extensions from seeing requests.
function setNetworkExtensions(targetSession, enabled) {
    if (networkExtensionSessions.has(targetSession) === enabled) return;
    if (enabled) {
        networkExtensionSessions.add(targetSession);
        if (blockerInstance && blockerInstance.isBlockingEnabled(targetSession)) {
            blockerInstance.disableBlockingInSession(targetSession);
            attachAdblockerIpc();
        }
        for (const event of ['onBeforeRequest', 'onBeforeSendHeaders', 'onSendHeaders',
            'onHeadersReceived', 'onResponseStarted', 'onBeforeRedirect', 'onCompleted', 'onErrorOccurred']) {
            targetSession.webRequest[event](null);
        }
    } else {
        networkExtensionSessions.delete(targetSession);
        // Restore synchronous rules immediately; initialise cosmetics afterwards.
        attachNetworkHandlers(targetSession);
        attachHeaderHandler(targetSession);
        initialiseAdblocker(targetSession);
    }
}

function hasNetworkExtensions(targetSession) {
    return networkExtensionSessions.has(targetSession);
}

// Electron keeps a single listener per webRequest event, so the ad-blocker's
// onBeforeRequest and onHeadersReceived would replace ours. Register one
// listener for each that runs Fly's rules first, then hands over to the blocker.
function attachNetworkHandlers(targetSession) {
    if (!targetSession.webRequest || networkExtensionSessions.has(targetSession)) return;

    const blockingEnabled = () => blockerInstance && blockerInstance.isBlockingEnabled(targetSession);

    targetSession.webRequest.onBeforeRequest({ urls: ['<all_urls>'] }, (details, callback) => {
        // Route Google authentication requests through the WebLite sign-in flow
        const url = details.url || '';
        if (url.includes('flowName=GlifWebSignIn') && /^https?:\/\/accounts\.google\.com\//i.test(url)) {
            return callback({ redirectURL: url.replaceAll('flowName=GlifWebSignIn', 'flowName=WebLiteSignIn') });
        }
        if (blockingEnabled()) return blockerInstance.onBeforeRequest(details, callback);
        callback({});
    });

    targetSession.webRequest.onHeadersReceived({ urls: ['<all_urls>'] }, (details, callback) => {
        // Intercept main frame navigation to PDF documents and route to built-in viewer
        if ((details.resourceType === 'main_frame' || details.resourceType === 'mainFrame') && details.url) {
            const viewerPrefix = 'file://' + path.join(__dirname, 'assets', 'pdfjs', 'viewer.html');
            if (!details.url.startsWith(viewerPrefix)) {
                const headers = details.responseHeaders || {};
                let isPdf = false;
                for (const [headerKey, headerVal] of Object.entries(headers)) {
                    if (headerKey.toLowerCase() === 'content-type') {
                        const valStr = Array.isArray(headerVal) ? headerVal.join(' ') : String(headerVal);
                        const lowerVal = valStr.toLowerCase();
                        if (lowerVal.includes('application/pdf') || lowerVal.includes('application/x-pdf')) {
                            isPdf = true;
                            break;
                        }
                    }
                }
                if (isPdf) {
                    const viewerUrl = mintPdfViewerUrl(details.url);
                    return callback({ redirectURL: viewerUrl });
                }
            }
        }
        if (blockingEnabled()) return blockerInstance.onHeadersReceived(details, callback);
        callback({ responseHeaders: details.responseHeaders });
    });
}

async function applyTrustedDomainExceptions() {
    if (!blockerInstance) return;
    const newFilters = buildExceptionFilters(trustedDomains);
    try {
        blockerInstance.updateFromDiff({
            removed: appliedExceptionFilters,
            added: newFilters
        });
        appliedExceptionFilters = newFilters;
    } catch (err) {
        console.warn('Failed to update trusted-domain ad-blocker exceptions:', err);
    }
}

function setTrustedDomains(domains) {
    const clean = Array.isArray(domains) ? domains.filter(Boolean) : [];
    if (trustedDomains.length === clean.length && trustedDomains.every((d, i) => d === clean[i])) {
        return;
    }
    trustedDomains = clean;
    applyTrustedDomainExceptions();
}

// Known IPC channels registered internally by @ghostery/adblocker-electron
const GHOSTERY_IPC_CHANNELS = [
    '@ghostery/adblocker/inject-cosmetic-filters',
    '@ghostery/adblocker/is-mutation-observer-enabled',
    '@ghostery/adblocker/cosmetic-filters'
];

// Ghostery removes these process-wide handlers when disabling one session.
// Keep them available for other workspaces and scope calls to native sessions.
function attachAdblockerIpc() {
    GHOSTERY_IPC_CHANNELS.forEach(channel => {
        try { ipcMain.removeHandler(channel); } catch (e) {}
    });
    const nativeSender = event => event.sender &&
        !networkExtensionSessions.has(event.sender.session) &&
        blockerInstance.isBlockingEnabled(event.sender.session);
    ipcMain.handle('@ghostery/adblocker/inject-cosmetic-filters', (event, ...args) => {
        if (nativeSender(event)) return blockerInstance.onInjectCosmeticFilters(event, ...args);
    });
    ipcMain.handle('@ghostery/adblocker/is-mutation-observer-enabled', event =>
        nativeSender(event) ? blockerInstance.onIsMutationObserverEnabled(event) : false);
}

// Sessions start together on a clean profile. Share the pending engine build so
// they cannot download, deserialize and retain separate copies of the filter set.
function getAdblocker() {
    if (!blockerInitialisation) {
        blockerInitialisation = (async () => {
            if (fs.existsSync(CACHE_PATH)) {
                try {
                    blockerInstance = ElectronBlocker.deserialize(fs.readFileSync(CACHE_PATH));
                } catch (_) {
                    console.warn('Adblocker cache version mismatch or corrupted file. Rebuilding cache...');
                    fs.unlinkSync(CACHE_PATH);
                }
            }
            if (!blockerInstance) {
                const built = await require('./adblocker-build')();
                blockerInstance = built.blocker;
                fs.writeFileSync(CACHE_PATH, built.cacheBytes);
            }
            blockerInstance.updateFromDiff({added: [
                'youtube.com#@#+js()',
                'x.com#@#+js()',
                'twitter.com#@#+js()'
            ]});
            await applyTrustedDomainExceptions();
            return blockerInstance;
        })().catch(err => {
            blockerInstance = null;
            blockerInitialisation = null;
            console.error('Failed to instantiate network filters:', err);
            return null;
        });
    }
    return blockerInitialisation;
}

async function initialiseAdblocker(targetSession) {
    if (!await getAdblocker() || networkExtensionSessions.has(targetSession)) return;

    try {
        if (ipcInitialized) {
            GHOSTERY_IPC_CHANNELS.forEach(channel => {
                try { ipcMain.removeHandler(channel); } catch (e) {}
            });
        }
        blockerInstance.enableBlockingInSession(targetSession);
        attachAdblockerIpc();
        attachNetworkHandlers(targetSession);
        ipcInitialized = true;
    } catch (err) {
        console.error('Failed to attach network filters to session:', err);
    }
}

function hardenWebviewPreferences(webPreferences) {
    webPreferences.webgl = true;
    webPreferences.accelerated2dCanvas = true;
    webPreferences.experimentalFeatures = false;
    webPreferences.sandbox = true;
    webPreferences.contextIsolation = true;
    webPreferences.nodeIntegration = false;
    webPreferences.nodeIntegrationInSubFrames = false;
    webPreferences.enableRemoteModule = false;
}

async function clearGoogleAuthCookies(targetSession) {
    if (!targetSession || !targetSession.cookies) return;
    try {
        const domains = ['google.com', 'accounts.google.com', 'mail.google.com'];
        for (const dom of domains) {
            const cookies = await targetSession.cookies.get({ domain: dom });
            for (const c of cookies) {
                if (c.name.startsWith('__Host-') || c.name === 'OTZ' || c.name === 'NID' || c.name.startsWith('__Secure-')) {
                    const scheme = c.secure ? 'https://' : 'http://';
                    const cookieUrl = scheme + c.domain.replace(/^\./, '') + c.path;
                    await targetSession.cookies.remove(cookieUrl, c.name);
                }
            }
        }
    } catch (e) {}
}

const activePdfTokens = new Map();
const PDF_TOKEN_TTL_MS = 30 * 60 * 1000;

function pruneExpiredPdfTokens() {
    const now = Date.now();
    for (const [tok, data] of activePdfTokens.entries()) {
        if (now - data.createdAt > PDF_TOKEN_TTL_MS) {
            activePdfTokens.delete(tok);
        }
    }
}

function isPdfExtension(targetPath) {
    if (!targetPath || typeof targetPath !== 'string') return false;
    try {
        const cleanPath = targetPath.split('?')[0].split('#')[0];
        const ext = path.extname(cleanPath).toLowerCase();
        return ext === '.pdf';
    } catch (e) {
        return false;
    }
}

function mintPdfViewerUrl(targetUrl) {
    if (!targetUrl || typeof targetUrl !== 'string') return '';
    pruneExpiredPdfTokens();

    const token = crypto.randomUUID();
    activePdfTokens.set(token, {
        url: targetUrl,
        createdAt: Date.now()
    });

    const viewerPath = path.join(__dirname, 'assets', 'pdfjs', 'viewer.html');
    return `file://${viewerPath}?file=${encodeURIComponent(targetUrl)}&token=${token}`;
}

function validatePdfAccess(token, targetUrl, senderUrl) {
    const viewerPath = path.join(__dirname, 'assets', 'pdfjs', 'viewer.html');
    const viewerPrefix = 'file://' + viewerPath;

    // 1. Origin verification: sender must be the vendored viewer
    if (!senderUrl || typeof senderUrl !== 'string' || !senderUrl.startsWith(viewerPrefix)) {
        throw new Error('Authorisation failure: PDF bridge is restricted to the built-in PDF viewer.');
    }

    // 2. Token verification: token must exist and match target URL
    if (!token || typeof token !== 'string' || !activePdfTokens.has(token)) {
        throw new Error('Authorisation failure: Missing or invalid PDF capability token.');
    }

    const tokenData = activePdfTokens.get(token);
    if (!tokenData || tokenData.url !== targetUrl) {
        throw new Error('Authorisation failure: Capability token does not match requested PDF target.');
    }

    // 3. Path verification: local files must strictly possess a .pdf extension
    if (targetUrl.startsWith('file://') || targetUrl.startsWith('/')) {
        let filePath = targetUrl;
        if (targetUrl.startsWith('file://')) {
            try {
                filePath = new URL(targetUrl).pathname;
            } catch (e) {
                filePath = targetUrl.replace(/^file:\/\//, '');
            }
        }
        filePath = decodeURIComponent(filePath);
        if (!isPdfExtension(filePath)) {
            throw new Error('Authorisation failure: Target local file must possess a .pdf extension.');
        }
    }
}

module.exports = {
    hardenSession,
    setNetworkExtensions,
    hasNetworkExtensions,
    hardenWebviewPreferences,
    setTrustedDomains,
    isTrustedDomain,
    applySpellcheckerLanguage,
    clearGoogleAuthCookies,
    setGoogleSignInUserAgent,
    getGoogleSignInUserAgent,
    isGoogleSignInRequest,
    attachNetworkHandlers,
    mintPdfViewerUrl,
    validatePdfAccess
};
