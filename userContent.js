// userContent.js
// Local User-Script and User-Style injection manager for Fly Browser
// Monitors ~/.config/fly-browser/scripts and ~/.config/fly-browser/styles
// Injects matching .user.js and .user.css on dom-ready without extension runtime overhead

const fs = require('fs');
const path = require('path');
const os = require('os');
const { shell, ipcMain, net } = require('electron');
const dns = require('dns');
const nodeNet = require('net');
const crypto = require('crypto');
let UndiciAgent = null;
try {
    UndiciAgent = require('undici').Agent;
} catch {}

const defaultBaseDir = path.join(os.homedir(), '.config', 'fly-browser');
let scriptsDir = path.join(defaultBaseDir, 'scripts');
let stylesDir = path.join(defaultBaseDir, 'styles');
let storageDir = path.join(defaultBaseDir, 'script-storage');
let cachedMainWindow = null;

let loadedScripts = [];
let loadedStyles = [];

let scriptsWatcher = null;
let stylesWatcher = null;
let debounceTimerScripts = null;
let debounceTimerStyles = null;

const scriptStorageCache = new Map();
const activeTokens = new Map();

function sanitizeScriptId(scriptId) {
    return String(scriptId || 'default').replace(/[^a-zA-Z0-9._-]/g, '_');
}

function getScriptStoragePath(scriptId) {
    const cleanId = sanitizeScriptId(scriptId);
    return path.join(storageDir, `${cleanId}.json`);
}

function loadScriptStorage(scriptId) {
    const cleanId = sanitizeScriptId(scriptId);
    if (scriptStorageCache.has(cleanId)) {
        return scriptStorageCache.get(cleanId);
    }
    const filePath = getScriptStoragePath(cleanId);
    let data = {};
    try {
        if (fs.existsSync(filePath)) {
            data = JSON.parse(fs.readFileSync(filePath, 'utf8'));
        }
    } catch (e) {
        data = {};
    }
    scriptStorageCache.set(cleanId, data);
    return data;
}

function saveScriptStorage(scriptId, store) {
    const cleanId = sanitizeScriptId(scriptId);
    scriptStorageCache.set(cleanId, store);
    try {
        if (!fs.existsSync(storageDir)) {
            fs.mkdirSync(storageDir, { recursive: true });
        }
        const filePath = getScriptStoragePath(cleanId);
        fs.writeFileSync(filePath, JSON.stringify(store, null, 2), 'utf8');
    } catch (err) {
        console.error(`Failed to save script storage for ${cleanId}:`, err);
    }
}

function setScriptValue(scriptId, key, value) {
    const store = loadScriptStorage(scriptId);
    store[String(key)] = value;
    saveScriptStorage(scriptId, store);
}

function deleteScriptValue(scriptId, key) {
    const store = loadScriptStorage(scriptId);
    delete store[String(key)];
    saveScriptStorage(scriptId, store);
}

function generateScriptToken(scriptId, pageHostname = '', connects = [], grants = []) {
    const token = crypto.randomUUID();
    activeTokens.set(token, {
        scriptId: sanitizeScriptId(scriptId),
        pageHostname: String(pageHostname || '').toLowerCase(),
        connects: Array.isArray(connects) ? connects.map(c => String(c).trim()).filter(Boolean) : [],
        grants: Array.isArray(grants) ? grants.map(g => String(g).trim()).filter(Boolean) : [],
        createdAt: Date.now()
    });
    if (activeTokens.size > 5000) {
        const now = Date.now();
        for (const [tok, data] of activeTokens.entries()) {
            if (now - data.createdAt > 24 * 60 * 60 * 1000) {
                activeTokens.delete(tok);
            }
        }
    }
    return token;
}

function isPrivateOrBlockedIP(ip) {
    if (!ip || typeof ip !== 'string') return true;
    const cleanIp = ip.replace(/^\[|\]$/g, '').trim().toLowerCase();

    // Check IPv4-mapped IPv6 addresses (e.g. ::ffff:127.0.0.1)
    if (cleanIp.startsWith('::ffff:')) {
        const remaining = cleanIp.slice(7);
        if (nodeNet.isIPv4(remaining)) {
            return isPrivateOrBlockedIP(remaining);
        }
    }

    if (nodeNet.isIPv4(cleanIp)) {
        const parts = cleanIp.split('.').map(p => parseInt(p, 10));
        if (parts.length !== 4 || parts.some(p => isNaN(p) || p < 0 || p > 255)) {
            return true;
        }
        const [a, b, c, d] = parts;

        // 0.0.0.0/8 (Current network / default route)
        if (a === 0) return true;
        // 10.0.0.0/8 (Private Class A)
        if (a === 10) return true;
        // 127.0.0.0/8 (Loopback)
        if (a === 127) return true;
        // 100.64.0.0/10 (Shared address space / Carrier-grade NAT)
        if (a === 100 && (b >= 64 && b <= 127)) return true;
        // 169.254.0.0/16 (Link-local / Cloud metadata)
        if (a === 169 && b === 254) return true;
        // 172.16.0.0/12 (Private Class B)
        if (a === 172 && (b >= 16 && b <= 31)) return true;
        // 192.0.0.0/24 (IETF assignments)
        if (a === 192 && b === 0 && c === 0) return true;
        // 192.0.2.0/24 (TEST-NET-1)
        if (a === 192 && b === 0 && c === 2) return true;
        // 192.168.0.0/16 (Private Class C)
        if (a === 192 && b === 168) return true;
        // 198.18.0.0/15 (Network benchmark testing)
        if (a === 198 && (b === 18 || b === 19)) return true;
        // 198.51.100.0/24 (TEST-NET-2)
        if (a === 198 && b === 51 && c === 100) return true;
        // 203.0.113.0/24 (TEST-NET-3)
        if (a === 203 && b === 0 && c === 113) return true;
        // 224.0.0.0/4 (Multicast)
        if (a >= 224 && a <= 239) return true;
        // 240.0.0.0/4 (Reserved / Broadcast)
        if (a >= 240) return true;

        return false;
    }

    if (nodeNet.isIPv6(cleanIp)) {
        // Loopback and unspecified
        if (cleanIp === '::1' || cleanIp === '::' || cleanIp === '0:0:0:0:0:0:0:1' || cleanIp === '0:0:0:0:0:0:0:0') {
            return true;
        }
        // Unique Local Address (ULA: fc00::/7)
        if (cleanIp.startsWith('fc') || cleanIp.startsWith('fd')) {
            return true;
        }
        // Link-local (fe80::/10)
        if (/^fe[89ab][0-9a-f]/i.test(cleanIp) || cleanIp.startsWith('fe80:')) {
            return true;
        }
        // Multicast (ff00::/8)
        if (cleanIp.startsWith('ff')) {
            return true;
        }
        // Documentation prefix (2001:db8::/32)
        if (cleanIp.startsWith('2001:db8:') || cleanIp === '2001:db8::') {
            return true;
        }
        // Discard prefix (100::/64)
        if (cleanIp.startsWith('100::')) {
            return true;
        }

        return false;
    }

    return true;
}

function isHostAllowed(targetHostname, pageHostname, allowedConnects) {
    if (!targetHostname) return false;
    const target = targetHostname.toLowerCase();
    const page = (pageHostname || '').toLowerCase();

    // Same-origin and subdomains of the hosting page are implicitly permitted
    if (page && (target === page || target.endsWith('.' + page))) {
        return true;
    }

    if (!Array.isArray(allowedConnects) || allowedConnects.length === 0) {
        return false;
    }

    for (const rule of allowedConnects) {
        let pattern = String(rule || '').trim().toLowerCase();
        if (!pattern) continue;

        if (pattern.includes('://')) {
            try {
                pattern = new URL(pattern).hostname.toLowerCase();
            } catch {
                // Ignore parse errors, continue with pattern
            }
        }

        // Universal wildcard permits any public host
        if (pattern === '*') {
            return true;
        }

        // 'self' keyword explicitly matches the hosting page
        if (pattern === 'self') {
            if (page && (target === page || target.endsWith('.' + page))) {
                return true;
            }
            continue;
        }

        // Wildcard subdomain prefix (e.g. *.example.com)
        if (pattern.startsWith('*.')) {
            const root = pattern.slice(2);
            if (target === root || target.endsWith('.' + root)) {
                return true;
            }
            continue;
        }

        // Exact host match or parent domain matching all subdomains
        if (target === pattern || target.endsWith('.' + pattern)) {
            return true;
        }
    }

    return false;
}

async function validateRequestDestination(targetUrl, tokenData) {
    if (!targetUrl || typeof targetUrl !== 'string') {
        throw new Error('Valid URL string is required for request');
    }

    let parsedUrl;
    try {
        parsedUrl = new URL(targetUrl);
    } catch {
        throw new Error(`Invalid URL provided: "${targetUrl}"`);
    }

    const protocol = parsedUrl.protocol.toLowerCase();
    if (protocol !== 'http:' && protocol !== 'https:') {
        throw new Error(`Unsupported protocol "${protocol}". Only http: and https: requests are permitted`);
    }

    // Verify @grant authorisation if grants are declared by the userscript
    if (tokenData && Array.isArray(tokenData.grants) && tokenData.grants.length > 0) {
        const hasNetGrant = tokenData.grants.some(g => {
            const lower = String(g).trim().toLowerCase();
            return lower === 'gm_xmlhttprequest' || lower === 'gm.xmlhttprequest' || lower === '*' || lower === 'gm_*';
        });
        if (!hasNetGrant) {
            throw new Error('Permission denied: userscript does not declare @grant GM_xmlhttpRequest');
        }
    }

    const targetHostname = parsedUrl.hostname.toLowerCase();
    const cleanHost = targetHostname.replace(/^\[|\]$/g, '');

    // Validate declared @connect host permissions
    const pageHostname = (tokenData && tokenData.pageHostname) || '';
    const allowedConnects = (tokenData && tokenData.connects) || [];
    if (!isHostAllowed(targetHostname, pageHostname, allowedConnects)) {
        throw new Error(`Permission denied: host "${targetHostname}" is not declared in userscript @connect directives`);
    }

    // Reject internal or local domain names unconditionally
    if (
        cleanHost === 'localhost' ||
        cleanHost.endsWith('.localhost') ||
        cleanHost.endsWith('.local') ||
        cleanHost.endsWith('.lan') ||
        cleanHost.endsWith('.internal') ||
        cleanHost.endsWith('.home.arpa')
    ) {
        throw new Error(`SSRF protection: host "${targetHostname}" is a restricted local or internal domain`);
    }

    // Direct IP address validation against restricted ranges
    if (nodeNet.isIP(cleanHost)) {
        if (isPrivateOrBlockedIP(cleanHost)) {
            throw new Error(`SSRF protection: destination IP "${cleanHost}" belongs to a private or restricted network range`);
        }
        parsedUrl.pinnedIp = cleanHost;
        parsedUrl.pinnedFamily = nodeNet.isIP(cleanHost);
        return parsedUrl;
    }

    // Resolve domain name via DNS to prevent DNS rebinding or internal IP mapping
    let addresses = [];
    try {
        addresses = await dns.promises.lookup(cleanHost, { all: true });
    } catch (dnsErr) {
        throw new Error(`DNS resolution failed for host "${targetHostname}": ${dnsErr.message}`);
    }

    if (!addresses || addresses.length === 0) {
        throw new Error(`DNS resolution returned no records for host "${targetHostname}"`);
    }

    for (const record of addresses) {
        if (isPrivateOrBlockedIP(record.address)) {
            throw new Error(`SSRF protection: host "${targetHostname}" resolved to restricted IP ${record.address}`);
        }
    }

    parsedUrl.pinnedIp = addresses[0].address;
    parsedUrl.pinnedFamily = addresses[0].family;
    return parsedUrl;
}

function hasStorageGrant(tokenData) {
    if (!tokenData || !Array.isArray(tokenData.grants) || tokenData.grants.length === 0) {
        return true;
    }
    return tokenData.grants.some(g => {
        const lower = String(g).trim().toLowerCase();
        return lower === 'gm_setvalue' || lower === 'gm_getvalue' || lower === 'gm_deletevalue' ||
               lower === 'gm_listvalues' || lower === 'gm.*' || lower === '*' || lower === 'gm_*';
    });
}

function createPinnedDispatcher(targetValidation) {
    if (UndiciAgent && targetValidation && targetValidation.pinnedIp && !nodeNet.isIP(targetValidation.hostname)) {
        return new UndiciAgent({
            connect: {
                lookup: (hostname, opts, cb) => {
                    cb(null, [{ address: targetValidation.pinnedIp, family: targetValidation.pinnedFamily }]);
                }
            }
        });
    }
    return null;
}

async function executeGmXmlHttpRequest(details, tokenData = null) {
    if (!details || typeof details !== 'object' || !details.url) {
        throw new Error('Valid URL is required for GM_xmlhttpRequest');
    }

    let currentUrl = details.url;
    let validated = await validateRequestDestination(currentUrl, tokenData);

    const method = (details.method || 'GET').toUpperCase();
    const timeout = Number(details.timeout) || 30000;

    const headers = Object.assign({}, details.headers || {});
    if (!headers['User-Agent'] && !headers['user-agent']) {
        headers['User-Agent'] = 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36';
    }

    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);

    const init = {
        method,
        headers,
        signal: controller.signal
    };

    if (details.data && method !== 'GET' && method !== 'HEAD') {
        init.body = typeof details.data === 'object' ? JSON.stringify(details.data) : String(details.data);
    }

    const fetchFn = (typeof globalThis.fetch === 'function')
        ? globalThis.fetch
        : ((net && typeof net.fetch === 'function') ? net.fetch : null);

    if (!fetchFn) {
        clearTimeout(timer);
        throw new Error('No fetch implementation available in runtime');
    }

    const isManualRedirect = (details.redirect === 'manual' || details.redirect === 'error');
    const maxRedirects = 5;
    let redirectCount = 0;
    let activeDispatcher = null;

    try {
        let res;
        while (true) {
            const reqInit = Object.assign({}, init, { redirect: 'manual' });
            if (activeDispatcher && typeof activeDispatcher.destroy === 'function') {
                activeDispatcher.destroy();
                activeDispatcher = null;
            }
            activeDispatcher = createPinnedDispatcher(validated);
            if (activeDispatcher) {
                reqInit.dispatcher = activeDispatcher;
            }

            res = await fetchFn(currentUrl, reqInit);

            const isRedirectStatus = [301, 302, 303, 307, 308].includes(res.status);
            if (isRedirectStatus) {
                if (details.redirect === 'error') {
                    throw new Error('Encountered unexpected redirect with redirect=error');
                }
                if (!isManualRedirect) {
                    const locationHeader = res.headers && typeof res.headers.get === 'function'
                        ? res.headers.get('location')
                        : null;

                    if (locationHeader) {
                        if (redirectCount >= maxRedirects) {
                            throw new Error('Too many redirects encountered');
                        }
                        redirectCount++;
                        const nextUrl = new URL(locationHeader, currentUrl).href;
                        validated = await validateRequestDestination(nextUrl, tokenData);
                        currentUrl = nextUrl;
                        if (res.status === 303 || (res.status === 302 && init.method === 'POST')) {
                            init.method = 'GET';
                            delete init.body;
                        }
                        continue;
                    }
                }
            }
            break;
        }

        clearTimeout(timer);

        const responseText = await res.text();
        let responseHeaders = '';
        if (res.headers && typeof res.headers.forEach === 'function') {
            res.headers.forEach((val, key) => {
                responseHeaders += `${key}: ${val}\r\n`;
            });
        }

        return {
            status: res.status,
            statusText: res.statusText,
            readyState: 4,
            responseText,
            responseHeaders,
            finalUrl: currentUrl
        };
    } catch (err) {
        clearTimeout(timer);
        const isTimeout = err.name === 'AbortError' || err.code === 'ETIMEDOUT';
        throw {
            isTimeout,
            message: err.message || 'Request failed',
            status: isTimeout ? 0 : 0,
            statusText: isTimeout ? 'Timeout' : 'Error'
        };
    } finally {
        if (activeDispatcher && typeof activeDispatcher.destroy === 'function') {
            activeDispatcher.destroy();
            activeDispatcher = null;
        }
    }
}

function escapeRegex(str) {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function patternToRegex(pattern) {
    if (!pattern || typeof pattern !== 'string') return null;
    const trimmed = pattern.trim();
    if (!trimmed) return null;

    if (trimmed === '<all_urls>' || trimmed === '*' || trimmed === '*://*/*') {
        return /^https?:\/\/.+/i;
    }

    if (trimmed.startsWith('/') && trimmed.endsWith('/') && trimmed.length > 2) {
        try {
            return new RegExp(trimmed.slice(1, -1), 'i');
        } catch {
            return null;
        }
    }

    const matchScheme = trimmed.match(/^(\*|https?|file|ftp):\/\/(?:(\*|(?:\*\.)?[^\/]+))(\/.*)?$/i);
    if (matchScheme) {
        const scheme = matchScheme[1];
        const host = matchScheme[2];
        const pathPart = matchScheme[3] || '/*';

        const schemeRegex = (scheme === '*') ? 'https?' : escapeRegex(scheme);
        let hostRegex;
        if (host === '*') {
            hostRegex = '[^\\/]+';
        } else if (host.startsWith('*.')) {
            const rootDomain = escapeRegex(host.slice(2));
            hostRegex = `(?:[^\\/]+\\.)?${rootDomain}`;
        } else {
            hostRegex = escapeRegex(host);
        }

        const pathRegex = escapeRegex(pathPart).replace(/\\\*/g, '.*');
        return new RegExp(`^${schemeRegex}:\\/\\/${hostRegex}${pathRegex}$`, 'i');
    }

    const escaped = escapeRegex(trimmed).replace(/\\\*/g, '.*');
    return new RegExp(`^${escaped}$`, 'i');
}

function parseUserScriptMetadata(content, filename) {
    const meta = {
        name: path.basename(filename, path.extname(filename)).replace(/\.user$/, ''),
        version: '1.0',
        description: '',
        author: '',
        namespace: '',
        homepage: '',
        matches: [],
        includes: [],
        excludes: [],
        connects: [],
        grants: [],
        runAt: 'document-end',
        matchRegexes: [],
        includeRegexes: [],
        excludeRegexes: []
    };

    const blockMatch = content.match(/\/\/\s*==UserScript==([\s\S]*?)\/\/\s*==\/UserScript==/i);
    if (blockMatch) {
        const lines = blockMatch[1].split(/\r?\n/);
        for (const line of lines) {
            const matchDirective = line.match(/^\s*(?:\/\/|\*|\/\*)?\s*@([a-zA-Z0-9_-]+)(?:\s+(.*))?/);
            if (matchDirective) {
                const directive = matchDirective[1].toLowerCase();
                const value = (matchDirective[2] || '').trim();
                if (!value) continue;

                if (directive === 'name') {
                    meta.name = value;
                } else if (directive === 'version') {
                    meta.version = value;
                } else if (directive === 'description') {
                    meta.description = value;
                } else if (directive === 'author') {
                    meta.author = value;
                } else if (directive === 'namespace') {
                    meta.namespace = value;
                } else if (directive === 'homepage' || directive === 'homepageurl') {
                    meta.homepage = value;
                } else if (directive === 'match') {
                    meta.matches.push(value);
                } else if (directive === 'include') {
                    meta.includes.push(value);
                } else if (directive === 'exclude') {
                    meta.excludes.push(value);
                } else if (directive === 'connect') {
                    meta.connects.push(value);
                } else if (directive === 'grant') {
                    meta.grants.push(value);
                } else if (directive === 'run-at' || directive === 'runat') {
                    meta.runAt = value.toLowerCase();
                }
            }
        }
    }

    meta.matchRegexes = meta.matches.map(patternToRegex).filter(Boolean);
    meta.includeRegexes = meta.includes.map(patternToRegex).filter(Boolean);
    meta.excludeRegexes = meta.excludes.map(patternToRegex).filter(Boolean);

    return meta;
}

function parseUserStyleMetadata(content, filename) {
    const meta = {
        name: path.basename(filename, path.extname(filename)).replace(/\.user$/, ''),
        matches: [],
        includes: [],
        excludes: [],
        matchRegexes: [],
        includeRegexes: [],
        excludeRegexes: [],
        sections: [],
        hasMozDoc: false
    };

    const blockMatch = content.match(/(?:\/\*|\/\/)\s*==UserStyle==([\s\S]*?)(?:==\/UserStyle==\s*\*\/|\/\/\s*==\/UserStyle==)/i);
    if (blockMatch) {
        const lines = blockMatch[1].split(/\r?\n/);
        for (const line of lines) {
            const matchDirective = line.match(/^\s*(?:\/\/|\*|\/\*)?\s*@([a-zA-Z0-9_-]+)(?:\s+(.*))?/);
            if (matchDirective) {
                const directive = matchDirective[1].toLowerCase();
                const value = (matchDirective[2] || '').trim().replace(/\*\/$/, '').trim();
                if (!value) continue;

                if (directive === 'name') {
                    meta.name = value;
                } else if (directive === 'match') {
                    meta.matches.push(value);
                } else if (directive === 'include') {
                    meta.includes.push(value);
                } else if (directive === 'exclude') {
                    meta.excludes.push(value);
                }
            }
        }
    }

    meta.matchRegexes = meta.matches.map(patternToRegex).filter(Boolean);
    meta.includeRegexes = meta.includes.map(patternToRegex).filter(Boolean);
    meta.excludeRegexes = meta.excludes.map(patternToRegex).filter(Boolean);

    // Support classic @-moz-document scoping rules
    const mozDocRegex = /@-moz-document\s+([^{]+)\{([\s\S]*?)\}(?=\s*(?:@-moz-document|$))/gi;
    let match;
    while ((match = mozDocRegex.exec(content)) !== null) {
        meta.hasMozDoc = true;
        const conditionStr = match[1];
        const cssRules = match[2];
        const conditions = [];

        const fnRegex = /(domain|url-prefix|url|regexp)\(\s*["']?([^"')]+)["']?\s*\)/gi;
        let fnMatch;
        while ((fnMatch = fnRegex.exec(conditionStr)) !== null) {
            conditions.push({
                type: fnMatch[1].toLowerCase(),
                value: fnMatch[2].trim()
            });
        }

        meta.sections.push({
            conditions,
            css: cssRules
        });
    }

    return meta;
}

function urlMatchesRule(url, meta) {
    if (!url || typeof url !== 'string') return false;
    if (url.startsWith('about:') || url.startsWith('chrome:') || url.startsWith('devtools:')) {
        return false;
    }

    if (Array.isArray(meta.excludeRegexes) && meta.excludeRegexes.length > 0) {
        for (const regex of meta.excludeRegexes) {
            if (regex && regex.test(url)) return false;
        }
    }

    const hasExplicitMatches = (meta.matchRegexes && meta.matchRegexes.length > 0) ||
                               (meta.includeRegexes && meta.includeRegexes.length > 0);

    if (!hasExplicitMatches) {
        return url.startsWith('http://') || url.startsWith('https://');
    }

    if (Array.isArray(meta.matchRegexes)) {
        for (const regex of meta.matchRegexes) {
            if (regex && regex.test(url)) return true;
        }
    }

    if (Array.isArray(meta.includeRegexes)) {
        for (const regex of meta.includeRegexes) {
            if (regex && regex.test(url)) return true;
        }
    }

    return false;
}

function matchesMozDocCondition(url, hostname, condition) {
    const type = condition.type;
    const val = condition.value;

    if (type === 'domain') {
        const lowerHost = hostname.toLowerCase();
        const lowerVal = val.toLowerCase();
        return lowerHost === lowerVal || lowerHost.endsWith('.' + lowerVal);
    }
    if (type === 'url') {
        return url === val;
    }
    if (type === 'url-prefix') {
        return url.startsWith(val);
    }
    if (type === 'regexp') {
        try {
            const re = new RegExp(val, 'i');
            return re.test(url);
        } catch {
            return false;
        }
    }
    return false;
}

function reloadScripts() {
    const nextScripts = [];
    try {
        if (!fs.existsSync(scriptsDir)) {
            fs.mkdirSync(scriptsDir, { recursive: true });
        }
        const entries = fs.readdirSync(scriptsDir, { withFileTypes: true });
        for (const entry of entries) {
            if (!entry.isFile()) continue;
            if (entry.name.startsWith('.')) continue;
            if (!entry.name.endsWith('.js')) continue;

            const filePath = path.join(scriptsDir, entry.name);
            try {
                const code = fs.readFileSync(filePath, 'utf8');
                const meta = parseUserScriptMetadata(code, entry.name);
                nextScripts.push({
                    name: meta.name,
                    filename: entry.name,
                    filePath,
                    code,
                    meta
                });
            } catch (fileErr) {
                console.error(`Failed to read user script ${entry.name}:`, fileErr);
            }
        }
    } catch (err) {
        console.error('Failed to read scripts directory:', err);
    }

    loadedScripts = nextScripts;
    return loadedScripts.length;
}

function reloadStyles() {
    const nextStyles = [];
    try {
        if (!fs.existsSync(stylesDir)) {
            fs.mkdirSync(stylesDir, { recursive: true });
        }
        const entries = fs.readdirSync(stylesDir, { withFileTypes: true });
        for (const entry of entries) {
            if (!entry.isFile()) continue;
            if (entry.name.startsWith('.')) continue;
            if (!entry.name.endsWith('.css')) continue;

            const filePath = path.join(stylesDir, entry.name);
            try {
                const css = fs.readFileSync(filePath, 'utf8');
                const meta = parseUserStyleMetadata(css, entry.name);
                nextStyles.push({
                    name: meta.name,
                    filename: entry.name,
                    filePath,
                    css,
                    meta
                });
            } catch (fileErr) {
                console.error(`Failed to read user style ${entry.name}:`, fileErr);
            }
        }
    } catch (err) {
        console.error('Failed to read styles directory:', err);
    }

    loadedStyles = nextStyles;
    return loadedStyles.length;
}

function notifyRenderer(kind) {
    if (cachedMainWindow && !cachedMainWindow.isDestroyed()) {
        cachedMainWindow.webContents.send('user-content-updated', kind);
    }
}

function setupWatchers() {
    try {
        if (scriptsWatcher) scriptsWatcher.close();
        if (fs.existsSync(scriptsDir)) {
            scriptsWatcher = fs.watch(scriptsDir, (eventType, filename) => {
                if (debounceTimerScripts) clearTimeout(debounceTimerScripts);
                debounceTimerScripts = setTimeout(() => {
                    reloadScripts();
                    notifyRenderer('scripts');
                }, 200);
            });
        }
    } catch (err) {
        console.error('Failed to setup user scripts watcher:', err);
    }

    try {
        if (stylesWatcher) stylesWatcher.close();
        if (fs.existsSync(stylesDir)) {
            stylesWatcher = fs.watch(stylesDir, (eventType, filename) => {
                if (debounceTimerStyles) clearTimeout(debounceTimerStyles);
                debounceTimerStyles = setTimeout(() => {
                    reloadStyles();
                    notifyRenderer('styles');
                }, 200);
            });
        }
    } catch (err) {
        console.error('Failed to setup user styles watcher:', err);
    }
}

function createTemplateFilesIfEmpty() {
    try {
        const scriptTemplatePath = path.join(scriptsDir, 'template.user.js');
        if (!fs.existsSync(scriptTemplatePath)) {
            const sampleScript = `// ==UserScript==
// @name         Example User Script
// @match        https://example.com/*
// @run-at       document-end
// ==/UserScript==

// Local user script executed automatically on matching pages
console.log('[Fly UserScript] Active on:', window.location.href);
`;
            fs.writeFileSync(scriptTemplatePath, sampleScript, 'utf8');
        }

        const styleTemplatePath = path.join(stylesDir, 'template.user.css');
        if (!fs.existsSync(styleTemplatePath)) {
            const sampleStyle = `/* ==UserStyle==
@name           Example User Style
@match          https://example.com/*
==/UserStyle== */

/* Custom CSS injected into matching web pages */
body {
    /* outline: 2px solid #7aa2f7 !important; */
}
`;
            fs.writeFileSync(styleTemplatePath, sampleStyle, 'utf8');
        }
    } catch (err) {
        console.error('Failed to write template user files:', err);
    }
}

function getMatchingUserContent(targetUrl) {
    if (!targetUrl || typeof targetUrl !== 'string') {
        return { scripts: [], styles: [] };
    }

    let hostname = '';
    try {
        hostname = new URL(targetUrl).hostname.toLowerCase();
    } catch {
        return { scripts: [], styles: [] };
    }

    const matchingScripts = [];
    for (const item of loadedScripts) {
        if (urlMatchesRule(targetUrl, item.meta)) {
            const scriptId = item.filename.replace(/\.js$/, '');
            matchingScripts.push({
                name: item.name,
                filename: item.filename,
                scriptId: scriptId,
                code: item.code,
                runAt: item.meta.runAt,
                meta: item.meta,
                storage: loadScriptStorage(scriptId),
                token: generateScriptToken(scriptId, hostname, item.meta.connects, item.meta.grants)
            });
        }
    }

    const matchingStyles = [];
    for (const item of loadedStyles) {
        if (item.meta.hasMozDoc && item.meta.sections.length > 0) {
            let combinedSectionCss = '';
            for (const section of item.meta.sections) {
                const matchesAny = section.conditions.some(cond => matchesMozDocCondition(targetUrl, hostname, cond));
                if (matchesAny) {
                    combinedSectionCss += section.css + '\n';
                }
            }
            if (combinedSectionCss.trim()) {
                matchingStyles.push({
                    name: item.name,
                    filename: item.filename,
                    css: combinedSectionCss
                });
            }
        } else if (urlMatchesRule(targetUrl, item.meta)) {
            matchingStyles.push({
                name: item.name,
                filename: item.filename,
                css: item.css
            });
        }
    }

    return {
        scripts: matchingScripts,
        styles: matchingStyles
    };
}

function initializeUserContent(configDir, mainWindow) {
    scriptsDir = path.join(configDir, 'scripts');
    stylesDir = path.join(configDir, 'styles');
    storageDir = path.join(configDir, 'script-storage');
    cachedMainWindow = mainWindow;

    if (!fs.existsSync(scriptsDir)) fs.mkdirSync(scriptsDir, { recursive: true });
    if (!fs.existsSync(stylesDir)) fs.mkdirSync(stylesDir, { recursive: true });
    if (!fs.existsSync(storageDir)) fs.mkdirSync(storageDir, { recursive: true });

    createTemplateFilesIfEmpty();
    reloadScripts();
    reloadStyles();
    setupWatchers();

    ipcMain.handle('get-user-content-for-url', (event, url) => {
        return getMatchingUserContent(url);
    });

    ipcMain.handle('reload-user-content', () => {
        const scCount = reloadScripts();
        const stCount = reloadStyles();
        notifyRenderer('all');
        return { scriptsCount: scCount, stylesCount: stCount };
    });

    ipcMain.handle('open-user-scripts-dir', async () => {
        if (!fs.existsSync(scriptsDir)) fs.mkdirSync(scriptsDir, { recursive: true });
        return shell.openPath(scriptsDir);
    });

    ipcMain.handle('open-user-styles-dir', async () => {
        if (!fs.existsSync(stylesDir)) fs.mkdirSync(stylesDir, { recursive: true });
        return shell.openPath(stylesDir);
    });

    ipcMain.handle('open-user-script-storage-dir', async () => {
        if (!fs.existsSync(storageDir)) fs.mkdirSync(storageDir, { recursive: true });
        return shell.openPath(storageDir);
    });

    ipcMain.handle('get-user-content-summary', () => {
        return {
            scriptsCount: loadedScripts.length,
            stylesCount: loadedStyles.length,
            scripts: loadedScripts.map(s => ({ name: s.name, filename: s.filename })),
            styles: loadedStyles.map(s => ({ name: s.name, filename: s.filename }))
        };
    });

    function validateTokenSender(tokenData, event) {
        if (!tokenData || !event || !event.sender) {
            throw new Error('Authorisation failure: Unable to verify GM token sender');
        }

        let senderUrl = '';
        let senderHost = '';
        let senderProtocol = '';
        try {
            senderUrl = event.sender.getURL();
            if (senderUrl) {
                const parsed = new URL(senderUrl);
                senderHost = parsed.hostname.toLowerCase();
                senderProtocol = parsed.protocol.toLowerCase();
            }
        } catch (e) {}

        const expectedHost = tokenData.pageHostname || '';

        // If token was minted for a local file context, sender must strictly be on file: protocol
        if (!expectedHost) {
            if (senderProtocol !== 'file:' || senderHost !== '') {
                throw new Error('Authorisation failure: Origin mismatch for GM token');
            }
            return;
        }

        // For web origins, verify exact hostname match or subdomain match
        if (senderHost !== expectedHost && !senderHost.endsWith('.' + expectedHost)) {
            throw new Error('Authorisation failure: Origin mismatch for GM token');
        }
    }

    ipcMain.handle('gm-xmlhttprequest', async (event, payload) => {
        const { token, details } = payload || {};
        if (!token || !activeTokens.has(token)) {
            throw new Error('Unauthorized: Invalid or expired GM token');
        }
        const tokenData = activeTokens.get(token);
        validateTokenSender(tokenData, event);
        return executeGmXmlHttpRequest(details, tokenData);
    });

    ipcMain.handle('gm-storage-set', async (event, payload) => {
        const { token, scriptId, key, value } = payload || {};
        if (!token || !activeTokens.has(token)) {
            throw new Error('Unauthorized: Invalid or expired GM token');
        }
        const tokenData = activeTokens.get(token);
        validateTokenSender(tokenData, event);
        if (!tokenData || tokenData.scriptId !== sanitizeScriptId(scriptId)) {
            throw new Error('Unauthorized: Script ID mismatch');
        }
        if (!hasStorageGrant(tokenData)) {
            throw new Error('Permission denied: userscript does not declare storage @grant');
        }
        setScriptValue(scriptId, key, value);
        return true;
    });

    ipcMain.handle('gm-storage-delete', async (event, payload) => {
        const { token, scriptId, key } = payload || {};
        if (!token || !activeTokens.has(token)) {
            throw new Error('Unauthorized: Invalid or expired GM token');
        }
        const tokenData = activeTokens.get(token);
        validateTokenSender(tokenData, event);
        if (!tokenData || tokenData.scriptId !== sanitizeScriptId(scriptId)) {
            throw new Error('Unauthorized: Script ID mismatch');
        }
        if (!hasStorageGrant(tokenData)) {
            throw new Error('Permission denied: userscript does not declare storage @grant');
        }
        deleteScriptValue(scriptId, key);
        return true;
    });

    ipcMain.handle('gm-storage-get', async (event, payload) => {
        const { token, scriptId } = payload || {};
        if (!token || !activeTokens.has(token)) {
            throw new Error('Unauthorized: Invalid or expired GM token');
        }
        const tokenData = activeTokens.get(token);
        validateTokenSender(tokenData, event);
        if (!tokenData || tokenData.scriptId !== sanitizeScriptId(scriptId)) {
            throw new Error('Unauthorized: Script ID mismatch');
        }
        if (!hasStorageGrant(tokenData)) {
            throw new Error('Permission denied: userscript does not declare storage @grant');
        }
        return loadScriptStorage(scriptId);
    });
}

module.exports = {
    initializeUserContent,
    reloadScripts,
    reloadStyles,
    getMatchingUserContent,
    loadScriptStorage,
    saveScriptStorage,
    setScriptValue,
    deleteScriptValue,
    generateScriptToken,
    executeGmXmlHttpRequest,
    isPrivateOrBlockedIP,
    isHostAllowed,
    validateRequestDestination
};
