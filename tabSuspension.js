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
            // DevTools can detach our transport. Reattach only when no other debugger owns it.
            if (!entry.guest.debugger.isAttached()) { entry.guest.debugger.attach(); entry.owned = true; }
            await command(entry, 'Page.setWebLifecycleState', {state: 'active'});
            entry.frozen = false;
        });
    }
    function attach(guest) {
        const entry = {guest, visible: true, pinned: false, backgroundSince: now(), revision: 0,
            frozen: false, media: false, downloads: 0, ready: false, owned: false,
            requests: new Set(), sockets: new Set(), contexts: new Set(), queue: Promise.resolve()};
        guests.set(guest.id, entry);
        guest.once('destroyed', () => guests.delete(guest.id));
        guest.on('media-started-playing', () => { entry.media = true; resume(entry).catch(() => {}); });
        guest.on('media-paused', () => { entry.media = false; });
        guest.on('devtools-opened', () => { resume(entry).catch(() => {}); });
        guest.on('did-start-loading', () => { resume(entry).catch(() => {}); });
        guest.debugger.on('detach', () => {
            entry.ready = false; entry.owned = false;
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
            if (method === 'Network.loadingFinished' || method === 'Network.loadingFailed') entry.requests.delete(params.requestId);
        });
        enqueue(entry, async () => {
            try {
                if (guest.debugger.isAttached()) return;
                guest.debugger.attach(); entry.owned = true;
                await command(entry, 'Runtime.enable');
                await command(entry, 'Network.enable', {maxTotalBufferSize: 0, maxResourceBufferSize: 0});
                entry.ready = true;
            } catch { entry.ready = false; }
        });
    }
    // Inspect actual native objects instead of patching websites' networking/media APIs.
    // Check every default frame context. Cross-process frames make the page ineligible.
    async function hasBackgroundWork(entry) {
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
                for (const [type, predicate] of [
                    ['RTCPeerConnection', 'o.connectionState !== "closed"'],
                    ['MediaStreamTrack', 'o.readyState === "live"'],
                    ['AudioContext', 'o.state !== "closed"'],
                    ['WebSocket', 'o.readyState < 2'],
                    ['EventSource', 'o.readyState < 2'],
                    // WebTransport exposes no synchronous closed state; be conservative.
                    ['WebTransport', 'true'],
                    ['Worker', 'true'], ['SharedWorker', 'true'], ['IDBTransaction', 'true']
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
