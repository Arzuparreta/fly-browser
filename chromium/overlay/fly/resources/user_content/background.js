// Fly User Content: runs the files in Fly's scripts/ and styles/ folders.
//
// Native Fly writes content.js next to this file (self.FLY_USER_CONTENT =
// {version, flyVersion, scripts: [{file, text}], styles: [{file, text}],
// legacyStorage: {scriptId: {...}}}) and reloads this extension when the
// folders change. Metadata parsing and URL matching are the Electron
// userContent.js rules; the GM_* API is modules/userScriptShim.js, bridged to
// this worker through user script messaging.
'use strict';

importScripts('content.js');

const CONTENT = self.FLY_USER_CONTENT || {scripts: [], styles: [], legacyStorage: {}};

// ---- Shared with Electron userContent.js (pure functions) ----------------
function escapeRegex(str) {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
function patternToRegex(pattern) {
  if (!pattern || typeof pattern !== 'string') return null;
  const trimmed = pattern.trim();
  if (!trimmed) return null;
  if (trimmed === '<all_urls>' || trimmed === '*' || trimmed === '*://*/*')
    return /^https?:\/\/.+/i;
  if (trimmed.startsWith('/') && trimmed.endsWith('/') && trimmed.length > 2) {
    try { return new RegExp(trimmed.slice(1, -1), 'i'); } catch { return null; }
  }
  const m = trimmed.match(/^(\*|https?|file|ftp):\/\/(?:(\*|(?:\*\.)?[^\/]+))(\/.*)?$/i);
  if (m) {
    const scheme = m[1] === '*' ? 'https?' : escapeRegex(m[1]);
    const host = m[2];
    const hostRegex = host === '*' ? '[^\\/]+'
        : host.startsWith('*.') ? `(?:[^\\/]+\\.)?${escapeRegex(host.slice(2))}`
        : escapeRegex(host);
    const pathRegex = escapeRegex(m[3] || '/*').replace(/\\\*/g, '.*');
    return new RegExp(`^${scheme}:\\/\\/${hostRegex}${pathRegex}$`, 'i');
  }
  return new RegExp(`^${escapeRegex(trimmed).replace(/\\\*/g, '.*')}$`, 'i');
}
function baseName(file) {
  return file.replace(/\.[^.]+$/, '').replace(/\.user$/, '');
}
function parseScript(text, file) {
  const meta = {name: baseName(file), version: '1.0', description: '', author: '',
      namespace: '', homepage: '', matches: [], includes: [], excludes: [],
      connects: [], grants: [], runAt: 'document-end', injectInto: '', noframes: false};
  const block = text.match(/\/\/\s*==UserScript==([\s\S]*?)\/\/\s*==\/UserScript==/i);
  if (block) {
    for (const line of block[1].split(/\r?\n/)) {
      const d = line.match(/^\s*(?:\/\/|\*|\/\*)?\s*@([a-zA-Z0-9_-]+)(?:\s+(.*))?/);
      if (!d) continue;
      const key = d[1].toLowerCase();
      const value = (d[2] || '').trim();
      if (key === 'noframes') { meta.noframes = true; continue; }
      if (!value) continue;
      if (key === 'match') meta.matches.push(value);
      else if (key === 'include') meta.includes.push(value);
      else if (key === 'exclude') meta.excludes.push(value);
      else if (key === 'connect') meta.connects.push(value);
      else if (key === 'grant') meta.grants.push(value);
      else if (key === 'run-at' || key === 'runat') meta.runAt = value.toLowerCase();
      else if (key === 'inject-into') meta.injectInto = value.toLowerCase();
      else if (key === 'homepageurl') meta.homepage = value;
      else if (key in meta && typeof meta[key] === 'string') meta[key] = value;
    }
  }
  return meta;
}
function parseStyle(text, file) {
  const meta = {name: baseName(file), matches: [], includes: [], excludes: [], sections: []};
  const block = text.match(/(?:\/\*|\/\/)\s*==UserStyle==([\s\S]*?)(?:==\/UserStyle==\s*\*\/|\/\/\s*==\/UserStyle==)/i);
  if (block) {
    for (const line of block[1].split(/\r?\n/)) {
      const d = line.match(/^\s*(?:\/\/|\*|\/\*)?\s*@([a-zA-Z0-9_-]+)(?:\s+(.*))?/);
      if (!d) continue;
      const key = d[1].toLowerCase();
      const value = (d[2] || '').trim().replace(/\*\/$/, '').trim();
      if (!value) continue;
      if (key === 'name') meta.name = value;
      else if (key === 'match') meta.matches.push(value);
      else if (key === 'include') meta.includes.push(value);
      else if (key === 'exclude') meta.excludes.push(value);
    }
  }
  const mozDoc = /@-moz-document\s+([^{]+)\{([\s\S]*?)\}(?=\s*(?:@-moz-document|$))/gi;
  let m;
  while ((m = mozDoc.exec(text)) !== null) {
    const conditions = [];
    const fn = /(domain|url-prefix|url|regexp)\(\s*["']?([^"')]+)["']?\s*\)/gi;
    let c;
    while ((c = fn.exec(m[1])) !== null)
      conditions.push({type: c[1].toLowerCase(), value: c[2].trim()});
    meta.sections.push({conditions, css: m[2]});
  }
  return meta;
}
// ---- In-page matching (serialized into each registration) ----------------
const PAGE_HELPERS = `
${escapeRegex.toString()}
${patternToRegex.toString()}
function flyUrlMatches(url, meta) {
  if (!url || /^(about|chrome|devtools):/.test(url)) return false;
  const re = list => (list || []).map(patternToRegex).filter(Boolean);
  if (re(meta.excludes).some(r => r.test(url))) return false;
  const explicit = re(meta.matches).concat(re(meta.includes));
  if (!explicit.length) return url.startsWith('http://') || url.startsWith('https://');
  return explicit.some(r => r.test(url));
}
function flyMozDocMatches(url, host, c) {
  if (c.type === 'domain') { const h = host.toLowerCase(), v = c.value.toLowerCase(); return h === v || h.endsWith('.' + v); }
  if (c.type === 'url') return url === c.value;
  if (c.type === 'url-prefix') return url.startsWith(c.value);
  if (c.type === 'regexp') { try { return new RegExp(c.value, 'i').test(url); } catch { return false; } }
  return false;
}`;

function scriptId(file) {
  return file.replace(/\.js$/, '').replace(/[^a-zA-Z0-9._-]/g, '_');
}
function runAt(value) {
  return value === 'document-start' ? 'document_start'
      : value === 'document-idle' ? 'document_idle' : 'document_end';
}
// Scripts that need no GM API run with the page's own globals (Tampermonkey's
// "@grant none" rule, and "@inject-into page"); the rest run in the isolated
// user script world, which can message this worker.
function usesPageWorld(meta) {
  if (meta.injectInto === 'page') return true;
  if (meta.injectInto === 'content') return false;
  return meta.grants.length === 0 || meta.grants.every(g => g === 'none');
}

function scriptCode(item, meta, id, pageWorld) {
  const json = JSON.stringify;
  return `(function() {
'use strict';
${PAGE_HELPERS}
const __meta = ${json(meta)};
if (!flyUrlMatches(location.href, __meta)) return;
const __id = ${json(id)};
const __bridge = ${pageWorld ? 'null' : `(message) => chrome.runtime.sendMessage(Object.assign({id: __id}, message))`};
const __start = (__initial) => {
  const __store = Object.assign({}, __initial || {});
  const __listeners = new Map();
  let __next = 1;
  const __notify = (k, oldValue, value) => {
    for (const l of __listeners.values()) if (l.key === k) { try { l.fn(k, oldValue, value, false); } catch (e) {} }
  };
  const GM_getValue = (key, defaultValue) => key == null ? defaultValue
      : Object.prototype.hasOwnProperty.call(__store, String(key)) ? __store[String(key)] : defaultValue;
  const GM_setValue = (key, value) => {
    if (key == null) return;
    const k = String(key), old = __store[k];
    __store[k] = value; __notify(k, old, value);
    if (__bridge) __bridge({type: 'set', key: k, value}).catch(() => {});
  };
  const GM_deleteValue = (key) => {
    if (key == null) return;
    const k = String(key), old = __store[k];
    delete __store[k]; __notify(k, old, undefined);
    if (__bridge) __bridge({type: 'delete', key: k}).catch(() => {});
  };
  const GM_listValues = () => Object.keys(__store);
  const GM_addValueChangeListener = (name, fn) => { const id = __next++; __listeners.set(id, {key: String(name), fn}); return id; };
  const GM_removeValueChangeListener = (id) => { __listeners.delete(id); };
  const GM_xmlhttpRequest = (details) => {
    if (!details || typeof details !== 'object' || !details.url) throw new Error('GM_xmlhttpRequest requires a url');
    let aborted = false;
    const fail = (err) => {
      if (aborted) return;
      const e = {status: 0, statusText: 'Error', readyState: 4, responseText: '', response: null,
                 context: details.context, error: (err && err.message) || String(err || 'Request failed')};
      if (err && err.isTimeout && typeof details.ontimeout === 'function') { try { details.ontimeout(e); } catch (x) {} }
      else if (typeof details.onerror === 'function') { try { details.onerror(e); } catch (x) {} }
      if (typeof details.onloadend === 'function') { try { details.onloadend(e); } catch (x) {} }
    };
    if (!__bridge) { fail(new Error('GM_xmlhttpRequest needs @grant GM_xmlhttpRequest')); return {abort() {}}; }
    __bridge({type: 'xhr', page: location.href, request: {
        method: String(details.method || 'GET').toUpperCase(), url: new URL(details.url, location.href).href,
        headers: details.headers || {}, data: details.data == null ? null : String(details.data),
        timeout: Number(details.timeout) || 30000}}).then((res) => {
      if (aborted) return;
      if (!res || res.error) { fail(res || {}); return; }
      let response = res.responseText;
      if (details.responseType === 'json') { try { response = JSON.parse(res.responseText); } catch (e) { response = null; } }
      let responseXML = null;
      if (!details.responseType || details.responseType === 'document') {
        try { responseXML = new DOMParser().parseFromString(res.responseText, 'text/html'); } catch (e) {}
      }
      const full = {status: res.status, statusText: res.statusText, readyState: 4, responseText: res.responseText,
          response, responseXML, responseHeaders: res.responseHeaders || '', finalUrl: res.finalUrl || details.url,
          context: details.context,
          getResponseHeader(name) {
            const m = (this.responseHeaders || '').match(new RegExp('^' + String(name).replace(/[-/\\\\^$*+?.()|[\\]{}]/g, '\\\\$&') + ':\\\\s*(.*)$', 'im'));
            return m ? m[1].trim() : null;
          },
          getAllResponseHeaders() { return this.responseHeaders || ''; }};
      for (const cb of ['onreadystatechange', 'onload', 'onloadend'])
        if (typeof details[cb] === 'function') { try { details[cb](full); } catch (e) {} }
    }, fail);
    return {abort() { aborted = true; if (typeof details.onabort === 'function') { try { details.onabort({readyState: 0, status: 0, statusText: 'Aborted'}); } catch (e) {} } }};
  };
  const GM_addStyle = (css) => {
    if (!css || typeof css !== 'string') return null;
    const style = document.createElement('style');
    style.textContent = css;
    (document.head || document.documentElement || document.body).appendChild(style);
    return style;
  };
  const __menu = new Map();
  let __menuNext = 1;
  const GM_registerMenuCommand = (caption, onClick, accessKey) => { const id = __menuNext++; __menu.set(id, {caption, onClick, accessKey}); return id; };
  const GM_unregisterMenuCommand = (id) => { __menu.delete(id); };
  const GM_setClipboard = (data) => { if (navigator.clipboard) navigator.clipboard.writeText(String(data)).catch(() => {}); };
  const GM_log = (...args) => console.log('[Fly UserScript: ' + __meta.name + ']', ...args);
  const GM_openInTab = (url, options) => {
    if (__bridge) { __bridge({type: 'open', url: new URL(url, location.href).href, active: !(options && (options === true || options.active === false))}).catch(() => {}); return null; }
    return window.open(url, '_blank');
  };
  const GM_notification = (textOrOptions, title, image, onclick) => {
    const o = typeof textOrOptions === 'object' && textOrOptions ? textOrOptions : {text: String(textOrOptions || ''), title, image, onclick};
    if (__bridge) { __bridge({type: 'notify', title: o.title || __meta.name, text: o.text || ''}).catch(() => {}); return null; }
    console.log('[Fly UserScript Notification]', o.title || '', o.text || '');
    return null;
  };
  const GM_info = {script: {name: __meta.name, version: __meta.version, description: __meta.description,
      author: __meta.author, namespace: __meta.namespace, matches: __meta.matches, includes: __meta.includes,
      excludes: __meta.excludes, connects: __meta.connects, grants: __meta.grants, runAt: __meta.runAt},
      scriptHandler: 'Fly Browser', version: ${json(CONTENT.flyVersion || '')}};
  const unsafeWindow = window;
  const GM = {info: GM_info,
    getValue: (k, d) => Promise.resolve(GM_getValue(k, d)),
    setValue: (k, v) => { GM_setValue(k, v); return Promise.resolve(); },
    deleteValue: (k) => { GM_deleteValue(k); return Promise.resolve(); },
    listValues: () => Promise.resolve(GM_listValues()),
    xmlHttpRequest: (d) => new Promise((resolve, reject) => GM_xmlhttpRequest(Object.assign({}, d, {
      onload: (r) => { if (d && d.onload) d.onload(r); resolve(r); },
      onerror: (e) => { if (d && d.onerror) d.onerror(e); reject(e); },
      ontimeout: (e) => { if (d && d.ontimeout) d.ontimeout(e); reject(e); }}))),
    addStyle: (css) => Promise.resolve(GM_addStyle(css)),
    registerMenuCommand: (c, f, a) => Promise.resolve(GM_registerMenuCommand(c, f, a)),
    setClipboard: (d) => Promise.resolve(GM_setClipboard(d)),
    openInTab: (u, o) => Promise.resolve(GM_openInTab(u, o)),
    notification: (o) => Promise.resolve(GM_notification(o))};
  try {
    (function() {
${item.text}
    }).call(window);
  } catch (err) {
    console.error('[Fly UserScript: ' + __meta.name + ']', err);
  }
};
if (__bridge) __bridge({type: 'get'}).then(__start, () => __start({}));
else __start({});
})();
//# sourceURL=fly-userscript://${encodeURIComponent(meta.name.replace(/\s+/g, '_'))}.user.js`;
}

function styleCode(item, meta) {
  const json = JSON.stringify;
  return `(function() {
${PAGE_HELPERS}
const __meta = ${json({matches: meta.matches, includes: meta.includes, excludes: meta.excludes})};
const __sections = ${json(meta.sections)};
const __css = ${json(item.text)};
let css = '';
if (__sections.length) {
  let host = '';
  try { host = new URL(location.href).hostname; } catch (e) { return; }
  for (const s of __sections) if (s.conditions.some(c => flyMozDocMatches(location.href, host, c))) css += s.css + '\\n';
} else if (flyUrlMatches(location.href, __meta)) {
  css = __css;
}
if (!css.trim()) return;
const style = document.createElement('style');
style.dataset.flyUserStyle = ${json(meta.name)};
style.textContent = css;
(document.head || document.documentElement).appendChild(style);
})();`;
}

// ---- Registration ---------------------------------------------------------
async function register() {
  const stored = await chrome.storage.local.get('registered');
  if (stored.registered === CONTENT.version) return;
  await chrome.userScripts.configureWorld({messaging: true});
  const existing = await chrome.userScripts.getScripts();
  if (existing.length) await chrome.userScripts.unregister({ids: existing.map(s => s.id)});
  const registrations = [];
  const metas = {};
  for (const item of CONTENT.scripts) {
    const meta = parseScript(item.text, item.file);
    const id = scriptId(item.file);
    const pageWorld = usesPageWorld(meta);
    metas[id] = {connects: meta.connects, grants: meta.grants, name: meta.name};
    registrations.push({id: 'script:' + id, matches: ['http://*/*', 'https://*/*', 'file:///*'],
        js: [{code: scriptCode(item, meta, id, pageWorld)}], runAt: runAt(meta.runAt),
        allFrames: false, world: pageWorld ? 'MAIN' : 'USER_SCRIPT'});
  }
  for (const item of CONTENT.styles) {
    const meta = parseStyle(item.text, item.file);
    registrations.push({id: 'style:' + scriptId(item.file), matches: ['http://*/*', 'https://*/*', 'file:///*'],
        js: [{code: styleCode(item, meta)}], runAt: 'document_start', allFrames: false, world: 'USER_SCRIPT'});
  }
  if (registrations.length) await chrome.userScripts.register(registrations);
  // Values saved by Electron Fly (script-storage/) seed scripts that have none.
  for (const [id, values] of Object.entries(CONTENT.legacyStorage || {})) {
    const key = 'gm:' + id;
    const current = await chrome.storage.local.get(key);
    if (!current[key]) await chrome.storage.local.set({[key]: values});
  }
  await chrome.storage.local.set({registered: CONTENT.version, metas});
}

// ---- GM bridge --------------------------------------------------------------
function isPrivateHost(host) {
  const h = host.toLowerCase();
  if (h === 'localhost' || h.endsWith('.localhost') || h === '[::1]') return true;
  const m = h.match(/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  if (!m) return h.startsWith('[fc') || h.startsWith('[fd') || h.startsWith('[fe80');
  const [a, b] = [Number(m[1]), Number(m[2])];
  return a === 10 || a === 127 || a === 0 || (a === 169 && b === 254) ||
      (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}
function isHostAllowed(target, page, connects) {
  target = target.toLowerCase();
  page = (page || '').toLowerCase();
  if (page && (target === page || target.endsWith('.' + page))) return true;
  for (const rule of connects || []) {
    let pattern = String(rule || '').trim().toLowerCase();
    if (!pattern) continue;
    if (pattern.includes('://')) { try { pattern = new URL(pattern).hostname.toLowerCase(); } catch (e) {} }
    if (pattern === '*') return true;
    if (pattern === 'self') continue;
    if (pattern.startsWith('*.')) pattern = pattern.slice(2);
    if (target === pattern || target.endsWith('.' + pattern)) return true;
  }
  return false;
}
async function handle(message) {
  const {metas = {}} = await chrome.storage.local.get('metas');
  const meta = metas[message.id];
  if (!meta) return {error: 'Unknown script'};
  const key = 'gm:' + message.id;
  if (message.type === 'get') return (await chrome.storage.local.get(key))[key] || {};
  if (message.type === 'set' || message.type === 'delete') {
    const values = (await chrome.storage.local.get(key))[key] || {};
    if (message.type === 'set') values[message.key] = message.value; else delete values[message.key];
    await chrome.storage.local.set({[key]: values});
    return {ok: true};
  }
  if (message.type === 'open') {
    const url = new URL(message.url);
    if (!['http:', 'https:'].includes(url.protocol)) return {error: 'Only web pages can be opened'};
    await chrome.tabs.create({url: url.href, active: message.active !== false});
    return {ok: true};
  }
  if (message.type === 'notify') {
    chrome.notifications.create({type: 'basic', iconUrl: 'data:image/gif;base64,R0lGODlhAQABAAAAACw=',
        title: String(message.title || meta.name).slice(0, 200), message: String(message.text || '').slice(0, 1000)});
    return {ok: true};
  }
  if (message.type === 'xhr') {
    if (!meta.grants.some(g => g === 'GM_xmlhttpRequest' || g === 'GM.xmlHttpRequest'))
      return {error: 'Permission denied: userscript does not declare @grant GM_xmlhttpRequest'};
    const request = message.request;
    const target = new URL(request.url);
    const page = new URL(message.page);
    if (!['http:', 'https:'].includes(target.protocol)) return {error: 'Only http(s) requests are allowed'};
    if (!isHostAllowed(target.hostname, page.hostname, meta.connects))
      return {error: `Permission denied: ${target.hostname} is not declared in @connect`};
    if (isPrivateHost(target.hostname) && !isPrivateHost(page.hostname))
      return {error: 'Requests to local network addresses are blocked'};
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), Math.min(request.timeout || 30000, 120000));
    try {
      const response = await fetch(target.href, {method: request.method, headers: request.headers,
          body: ['GET', 'HEAD'].includes(request.method) ? undefined : request.data, signal: controller.signal,
          credentials: 'omit'});
      const text = await response.text();
      let headers = '';
      response.headers.forEach((value, name) => { headers += `${name}: ${value}\r\n`; });
      return {status: response.status, statusText: response.statusText, responseText: text,
          responseHeaders: headers, finalUrl: response.url};
    } catch (err) {
      return {error: err.name === 'AbortError' ? 'Request timed out' : String(err.message || err),
          isTimeout: err.name === 'AbortError'};
    } finally {
      clearTimeout(timer);
    }
  }
  return {error: 'Unknown request'};
}
chrome.runtime.onUserScriptMessage.addListener((message, sender, respond) => {
  handle(message).then(respond, (err) => respond({error: String(err && err.message || err)}));
  return true;
});
register().catch((err) => console.error('Fly User Content registration failed', err));
