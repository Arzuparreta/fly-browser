// Chromium lifecycle control. A failed eligibility check always leaves the document alive.
const TIMEOUTS = new Set([0, 5, 15, 30, 60]);
function migrateSuspensionConfig(config) {
    const value = Number(config.tab_suspend_timeout_minutes ?? config.tab_sleep_timeout_minutes ?? 15);
    const next = {...config, tab_suspend_timeout_minutes: TIMEOUTS.has(value) ? value : 15};
    delete next.tab_sleep_timeout_minutes;
    return next;
}

function createTabSuspension({getTimeout = () => 15, now = () => performance.now(), commandTimeout = 3000} = {}) {
    const guests = new Map();
    let timer;
    const enqueue = (entry, task) => {
        const next = entry.queue.then(task, task);
        entry.queue = next.catch(() => {});
        return next;
    };
    async function command(entry, method, params) {
        let timeout;
        try {
            return await Promise.race([
                entry.guest.debugger.sendCommand(method, params),
                new Promise((_, reject) => { timeout = setTimeout(() => reject(new Error(`Lifecycle command timed out: ${method}`)), commandTimeout); })
            ]);
        } finally { clearTimeout(timeout); }
    }
    function protectedPage(entry) {
        const guest = entry.guest;
        return guest.isDestroyed() || entry.visible || entry.pinned || entry.media || entry.downloads > 0 ||
            guest.isLoading() || guest.isDevToolsOpened() || guest.isCurrentlyAudible();
    }
    async function resume(entry) {
        entry.revision++;
        return enqueue(entry, async () => {
            if (!entry.frozen || entry.guest.isDestroyed()) return;
            // A fresh transport also needs Runtime/Network monitoring rebuilt.
            if (!await enableMonitoring(entry)) return;
            await command(entry, 'Page.setWebLifecycleState', {state: 'active'});
            entry.frozen = false;
        });
    }
    // Called inside the guest queue, including when resume needs a new transport.
    async function enableMonitoring(entry) {
        const guest = entry.guest;
        if (guest.isDestroyed()) return false;
        if (guest.debugger.isAttached()) {
            if (!entry.owned) return false; // Never take over another debugger.
            if (entry.ready) return true;
        } else {
            guest.debugger.attach();
            entry.owned = true;
        }
        const connection = entry.connection;
        entry.ready = false;
        entry.contexts.clear();
        entry.sockets.clear();
        // Runtime.enable must replay the current document's contexts even if a
        // resume already attached this transport or an earlier setup failed.
        for (const [method, params] of [
            ['Runtime.disable'], ['Runtime.enable'], ['Network.disable'],
            ['Network.enable', {maxTotalBufferSize: 0, maxResourceBufferSize: 0}]
        ]) {
            if (connection !== entry.connection || !entry.owned || guest.isDestroyed()) return false;
            await command(entry, method, params);
        }
        if (connection !== entry.connection || !entry.owned) return false;
        entry.ready = true;
        return true;
    }
    function restoreMonitoring(entry) {
        return enqueue(entry, async () => {
            try { await enableMonitoring(entry); }
            catch { entry.ready = false; }
        });
    }
    function attach(guest) {
        const entry = {guest, visible: true, pinned: false, backgroundSince: now(), revision: 0,
            frozen: false, media: false, downloads: 0, ready: false, owned: false, connection: 0, monitoringGap: false,
            requests: new Set(), staleRequests: new Set(), sockets: new Set(), contexts: new Set(), queue: Promise.resolve()};
        guests.set(guest.id, entry);
        guest.once('destroyed', () => guests.delete(guest.id));
        guest.on('media-started-playing', () => { entry.media = true; resume(entry).catch(() => {}); });
        guest.on('media-paused', () => { entry.media = false; });
        guest.on('devtools-opened', () => {
            resume(entry).catch(() => {});
            // Observe work started while DevTools is open as soon as possible.
            restoreMonitoring(entry);
        });
        guest.on('devtools-closed', () => { restoreMonitoring(entry); });
        guest.on('did-start-loading', () => { resume(entry).catch(() => {}); });
        guest.on('did-navigate', () => {
            // Requests from a document replaced during a gap cannot keep its new
            // document awake forever. Requests observed by this session survive.
            for (const id of entry.staleRequests) entry.requests.delete(id);
            entry.staleRequests.clear();
            if (entry.ready) entry.monitoringGap = false;
        });
        guest.debugger.on('detach', () => {
            entry.ready = false; entry.owned = false; entry.connection++;
            entry.monitoringGap = true;
            for (const id of entry.requests) entry.staleRequests.add(id);
            // Invalidate an eligibility scan, including one racing DevTools opening.
            entry.revision++;
        });
        guest.debugger.on('message', (_event, method, params, sessionId) => {
            if (sessionId) return;
            if (method === 'Runtime.executionContextsCleared') entry.contexts.clear();
            if (method === 'Runtime.executionContextDestroyed') entry.contexts.delete(params.executionContextId);
            if (method === 'Runtime.executionContextCreated' && params.context.auxData?.isDefault) entry.contexts.add(params.context.id);
            if (method === 'Network.requestWillBeSent') {
                entry.requests.add(params.requestId);
                entry.revision++;
                if (entry.frozen) resume(entry).catch(() => {});
            }
            if (method === 'Network.webSocketCreated' || method === 'Network.webTransportCreated') {
                entry.sockets.add(params.requestId ?? params.transportId);
                resume(entry).catch(() => {});
            }
            if (method === 'Network.webSocketClosed' || method === 'Network.webTransportClosed') entry.sockets.delete(params.requestId ?? params.transportId);
            if (method === 'Network.loadingFinished' || method === 'Network.loadingFailed') {
                entry.requests.delete(params.requestId);
                entry.staleRequests.delete(params.requestId);
            }
        });
        restoreMonitoring(entry);
    }
    // Network.enable cannot replay requests sent while the debugger was absent.
    // Pending native promises protect fetches awaiting headers or body consumption.
    // A bounded inspection is conservative on busy heaps and does not touch promises
    // with .then(), which would change rejection handling in the website.
    async function hasPendingPromises(entry, contextId, objectGroup) {
        const prototype = await command(entry, 'Runtime.evaluate', {expression: 'Promise.prototype', contextId, objectGroup});
        if (prototype.exceptionDetails || !prototype.result.objectId) return true;
        const {objects} = await command(entry, 'Runtime.queryObjects', {prototypeObjectId: prototype.result.objectId, objectGroup});
        const length = await command(entry, 'Runtime.callFunctionOn', {
            objectId: objects.objectId, functionDeclaration: 'function () { return this.length; }', returnByValue: true
        });
        if (length.exceptionDetails || !Number.isInteger(length.result.value) || length.result.value > 128) return true;
        if (!length.result.value) return false;
        const properties = await command(entry, 'Runtime.getProperties', {objectId: objects.objectId, ownProperties: true});
        for (const property of properties.result.filter(item => /^\d+$/.test(item.name))) {
            if (!property.value?.objectId) return true;
            const {internalProperties} = await command(entry, 'Runtime.getProperties', {objectId: property.value.objectId, ownProperties: true});
            const state = internalProperties?.find(item => item.name === '[[PromiseState]]')?.value?.value;
            if (state !== 'fulfilled' && state !== 'rejected') return true;
        }
        return false;
    }
    // Inspect actual native objects instead of patching websites' networking/media APIs.
    // Check every default frame context. Cross-process frames make the page ineligible.
    async function hasBackgroundWork(entry) {
        const connection = entry.connection;
        if (!entry.contexts.size || entry.requests.size || entry.sockets.size) return true;
        const {frameTree} = await command(entry, 'Page.getFrameTree');
        const frameCount = tree => 1 + (tree.childFrames || []).reduce((sum, child) => sum + frameCount(child), 0);
        if (frameCount(frameTree) !== entry.contexts.size) return true;
        for (const contextId of entry.contexts) {
            const group = `fly-lifecycle-${entry.guest.id}`;
            try {
                const locks = await command(entry, 'Runtime.evaluate', {
                    expression: `(async () => typeof navigator.locks?.query === 'function' ? (await navigator.locks.query()).held.length > 0 : false)()`,
                    contextId, awaitPromise: true, returnByValue: true
                });
                if (locks.exceptionDetails || locks.result.value !== false) return true;
                if (entry.monitoringGap && await hasPendingPromises(entry, contextId, group)) return true;
                for (const [type, predicate] of [
                    ['RTCPeerConnection', 'o.connectionState !== "closed"'],
                    ['MediaStreamTrack', 'o.readyState === "live"'],
                    ['AudioContext', 'o.state !== "closed"'],
                    ['WebSocket', 'o.readyState < 2'],
                    ['EventSource', 'o.readyState < 2'],
                    // WebTransport exposes no synchronous closed state; be conservative.
                    ['WebTransport', 'true'],
                    ['Worker', 'true'], ['SharedWorker', 'true'], ['IDBTransaction', 'true'],
                    // Reconcile requests missed during recovery without mutating APIs.
                    ...(entry.monitoringGap ? [
                        ['XMLHttpRequest', 'o.readyState > 0 && o.readyState < 4'],
                        ['ReadableStream', 'true']
                    ] : [])
                ]) {
                    const prototype = await command(entry, 'Runtime.evaluate', {
                        expression: `typeof ${type} === 'undefined' ? null : ${type}.prototype`, contextId, objectGroup: group
                    });
                    if (prototype.exceptionDetails) return true;
                    if (!prototype.result.objectId) continue;
                    const {objects} = await command(entry, 'Runtime.queryObjects', {prototypeObjectId: prototype.result.objectId, objectGroup: group});
                    const result = await command(entry, 'Runtime.callFunctionOn', {
                        objectId: objects.objectId,
                        functionDeclaration: `function () { return this.some(o => { try { if (o.constructor?.prototype === o) return false; return ${predicate}; } catch { return true; } }); }`,
                        returnByValue: true
                    });
                    if (result.exceptionDetails || result.result.value !== false) return true;
                }
            } finally { await command(entry, 'Runtime.releaseObjectGroup', {objectGroup: group}); }
        }
        if (connection !== entry.connection) return true;
        entry.monitoringGap = false;
        return false;
    }
    async function suspend(entry) {
        const revision = entry.revision;
        return enqueue(entry, async () => {
            try {
                if (entry.frozen || !entry.ready || !entry.owned || protectedPage(entry)) return false;
                if (await hasBackgroundWork(entry)) return false;
                if (entry.revision !== revision || protectedPage(entry) || entry.requests.size || entry.sockets.size) return false;
                await command(entry, 'Page.setWebLifecycleState', {state: 'frozen'});
                entry.frozen = true;
                // Visibility can change while Chromium processes the command.
                if (entry.revision !== revision || protectedPage(entry)) {
                    await command(entry, 'Page.setWebLifecycleState', {state: 'active'});
                    entry.frozen = false;
                    return false;
                }
                return true;
            } catch {
                entry.ready = false;
                // A timed-out freeze may still reach Chromium: queue a compensating resume.
                if (!entry.guest.isDestroyed() && entry.owned) {
                    try { await command(entry, 'Page.setWebLifecycleState', {state: 'active'}); entry.frozen = false; }
                    catch { entry.frozen = true; }
                }
                return false;
            }
        });
    }
    async function setPolicy(id, {visible, pinned}) {
        const entry = guests.get(id);
        if (!entry) return;
        if (entry.visible !== visible) {
            entry.backgroundSince = now();
            entry.revision++;
        }
        if (entry.pinned !== pinned) entry.revision++;
        entry.visible = visible; entry.pinned = pinned;
        if (visible || pinned) await resume(entry);
    }
    async function scan(manual = false) {
        const minutes = getTimeout();
        await Promise.all([...guests.values()].map(entry => {
            if (!manual && minutes <= 0) return resume(entry).catch(() => {});
            if (!manual && now() - entry.backgroundSince < minutes * 60000) return;
            return suspend(entry);
        }));
    }
    return {
        attach, setPolicy,
        resume: id => { const entry = guests.get(id); if (entry) entry.backgroundSince = now(); return entry ? resume(entry) : Promise.resolve(); },
        scan,
        trackDownload: (id, item) => {
            const entry = guests.get(id);
            if (!entry) return;
            entry.downloads++; resume(entry).catch(() => {});
            item.once('done', () => { entry.downloads--; });
        },
        stats: () => ({frozenTabs: [...guests.values()].filter(entry => entry.frozen).length}),
        start() { if (!timer) { timer = setInterval(() => scan().catch(() => {}), 30000); timer.unref(); } },
        stop() { clearInterval(timer); timer = null; }
    };
}
module.exports = {createTabSuspension, migrateSuspensionConfig};
