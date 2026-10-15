const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const {createTabSuspension, migrateSuspensionConfig} = require('../tabSuspension');

function fixture() {
    let clock = 0;
    const calls = [];
    let contextId = 0;
    const debuggerAPI = new EventEmitter();
    debuggerAPI.attached = false;
    debuggerAPI.isAttached = () => debuggerAPI.attached;
    debuggerAPI.attach = () => { debuggerAPI.attached = true; };
    debuggerAPI.sendCommand = async (method, params) => {
        calls.push([method, params]);
        if (method === 'Runtime.enable') debuggerAPI.emit('message', {}, 'Runtime.executionContextCreated', {context: {id: ++contextId, auxData: {isDefault: true}}});
        if (method === 'Page.getFrameTree') return {frameTree: {frame: {id: 'top'}}};
        if (method === 'Runtime.evaluate') return params.awaitPromise ? {result: {value: false}} : {result: {subtype: 'null', value: null}};
        return {};
    };
    const guest = new EventEmitter();
    Object.assign(guest, {id: 1, debugger: debuggerAPI,
        isDestroyed: () => false, isLoading: () => false, isDevToolsOpened: () => false,
        isCurrentlyAudible: () => false, isBeingCaptured: () => false});
    let timeout = 15;
    const manager = createTabSuspension({getTimeout: () => timeout, now: () => clock, commandTimeout: 50});
    manager.attach(guest);
    return {manager, guest, calls, tick: ms => {clock += ms;}, setTimeout: value => {timeout = value;}};
}
(async () => {
    assert.equal(migrateSuspensionConfig({tab_sleep_timeout_minutes: 0}).tab_suspend_timeout_minutes, 0);
    assert.equal(migrateSuspensionConfig({tab_sleep_timeout_minutes: 5, tab_suspend_timeout_minutes: 30}).tab_suspend_timeout_minutes, 30);
    assert.equal(migrateSuspensionConfig({tab_suspend_timeout_minutes: 'invalid'}).tab_suspend_timeout_minutes, 15);
    assert(!Object.hasOwn(migrateSuspensionConfig({tab_sleep_timeout_minutes: 15}), 'tab_sleep_timeout_minutes'));
    const f = fixture();
    await f.manager.setPolicy(1, {visible: false, pinned: false});
    f.tick(14 * 60000); await f.manager.scan(); assert.equal(f.manager.stats().frozenTabs, 0);
    // Background duration starts when hidden, not when last selected.
    await f.manager.setPolicy(1, {visible: true, pinned: false});
    await f.manager.setPolicy(1, {visible: false, pinned: false});
    f.tick(60000); await f.manager.scan(); assert.equal(f.manager.stats().frozenTabs, 0);
    f.tick(14 * 60000); await f.manager.scan(); assert.equal(f.manager.stats().frozenTabs, 1);
    await f.manager.setPolicy(1, {visible: true, pinned: false}); assert.equal(f.manager.stats().frozenTabs, 0);
    await f.manager.setPolicy(1, {visible: false, pinned: true});
    await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0);
    await f.manager.setPolicy(1, {visible: false, pinned: false});
    for (const method of ['isLoading', 'isDevToolsOpened', 'isCurrentlyAudible']) {
        f.guest[method] = () => true;
        await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0, method);
        f.guest[method] = () => false;
    }
    f.guest.emit('media-started-playing'); await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0);
    f.guest.emit('media-paused');
    const download = new EventEmitter(); f.manager.trackDownload(1, download);
    await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0);
    download.emit('done');
    f.guest.debugger.emit('message', {}, 'Network.requestWillBeSent', {requestId: 'upload'});
    await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0);
    f.guest.debugger.emit('message', {}, 'Network.loadingFinished', {requestId: 'upload'});
    await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 1);
    f.setTimeout(0); await f.manager.scan(); assert.equal(f.manager.stats().frozenTabs, 0);
    // Foregrounding during an asynchronous eligibility query cancels suspension.
    const realCommand = f.guest.debugger.sendCommand;
    let release;
    f.guest.debugger.sendCommand = async (method, params) => {
        if (method === 'Page.getFrameTree') await new Promise(resolve => {release = resolve;});
        return realCommand(method, params);
    };
    const suspension = f.manager.scan(true);
    await new Promise(resolve => setImmediate(resolve));
    const resumed = f.manager.setPolicy(1, {visible: true, pinned: false});
    release(); await Promise.all([suspension, resumed]); assert.equal(f.manager.stats().frozenTabs, 0);
    // Unknown or detached targets never fall back to unloading.
    f.guest.debugger.emit('detach');
    await f.manager.setPolicy(1, {visible: false, pinned: false});
    await f.manager.scan(true); assert.equal(f.manager.stats().frozenTabs, 0);
    f.guest.emit('destroyed'); assert.equal(f.manager.stats().frozenTabs, 0);
    // Opening DevTools disconnects Electron's debugger. Closing it must restore
    // both manual and automatic suspension without replacing the live page.
    const tools = fixture();
    await tools.manager.setPolicy(1, {visible: true, pinned: false});
    tools.guest.isDevToolsOpened = () => true;
    tools.guest.debugger.attached = false;
    tools.guest.debugger.emit('detach', {}, 'replaced_with_devtools');
    tools.guest.emit('devtools-opened');
    await tools.manager.scan(true);
    assert.equal(tools.manager.stats().frozenTabs, 0);
    tools.guest.debugger.emit('message', {}, 'Network.requestWillBeSent', {requestId: 'old-session'});
    tools.guest.debugger.emit('message', {}, 'Network.webSocketCreated', {requestId: 'old-socket'});
    tools.guest.isDevToolsOpened = () => false;
    tools.guest.emit('devtools-closed');
    await tools.manager.setPolicy(1, {visible: false, pinned: false});
    await tools.manager.scan(true);
    assert.equal(tools.manager.stats().frozenTabs, 1, 'Closing DevTools restores manual suspension');
    assert.equal(tools.calls.filter(([method]) => method === 'Runtime.enable').length, 2);
    assert.equal(tools.calls.filter(([method]) => method === 'Network.enable').length, 2);
    // Repeated close notifications reuse a healthy session and do not add listeners.
    tools.guest.emit('devtools-closed'); await tools.manager.scan(true);
    assert.equal(tools.calls.filter(([method]) => method === 'Runtime.enable').length, 2);
    assert.equal(tools.guest.debugger.listenerCount('message'), 1);
    assert.equal(tools.guest.debugger.listenerCount('detach'), 1);
    await tools.manager.setPolicy(1, {visible: true, pinned: false});
    await tools.manager.setPolicy(1, {visible: false, pinned: false});
    tools.tick(15 * 60000);
    await tools.manager.scan();
    assert.equal(tools.manager.stats().frozenTabs, 1, 'Closing DevTools restores automatic suspension');
    // A failed reconnect stays safe and can be retried on the next close event.
    const retry = fixture();
    await retry.manager.setPolicy(1, {visible: true, pinned: false});
    retry.guest.debugger.attached = false;
    retry.guest.debugger.emit('detach');
    const retryCommand = retry.guest.debugger.sendCommand;
    retry.guest.debugger.sendCommand = async (method, params) => {
        if (method === 'Network.enable') throw new Error('Reconnect failed');
        return retryCommand(method, params);
    };
    retry.guest.emit('devtools-closed');
    await retry.manager.setPolicy(1, {visible: false, pinned: false});
    await retry.manager.scan(true); assert.equal(retry.manager.stats().frozenTabs, 0);
    retry.guest.debugger.sendCommand = retryCommand;
    retry.guest.emit('devtools-closed');
    await retry.manager.scan(true); assert.equal(retry.manager.stats().frozenTabs, 1);
    // Closing DevTools never sends commands through a debugger owned elsewhere.
    const otherDebugger = fixture();
    await otherDebugger.manager.setPolicy(1, {visible: true, pinned: false});
    otherDebugger.guest.debugger.emit('detach');
    const commandCount = otherDebugger.calls.length;
    otherDebugger.guest.emit('devtools-closed');
    await otherDebugger.manager.scan(true);
    assert.equal(otherDebugger.calls.length, commandCount);
    // Detach during Runtime/Network setup invalidates the pending connection.
    const lostSetup = fixture();
    await lostSetup.manager.setPolicy(1, {visible: true, pinned: false});
    lostSetup.guest.debugger.attached = false;
    lostSetup.guest.debugger.emit('detach');
    const lostCommand = lostSetup.guest.debugger.sendCommand;
    lostSetup.guest.debugger.sendCommand = async (method, params) => {
        const result = await lostCommand(method, params);
        if (method === 'Runtime.enable') {
            lostSetup.guest.debugger.attached = false;
            lostSetup.guest.debugger.emit('detach');
        }
        return result;
    };
    lostSetup.guest.emit('devtools-closed');
    await lostSetup.manager.setPolicy(1, {visible: false, pinned: false});
    await lostSetup.manager.scan(true); assert.equal(lostSetup.manager.stats().frozenTabs, 0);
    lostSetup.guest.debugger.sendCommand = lostCommand;
    lostSetup.guest.emit('devtools-closed');
    await lostSetup.manager.scan(true); assert.equal(lostSetup.manager.stats().frozenTabs, 1);
    // Retained native objects and unknown frame contexts prevent forced suspension.
    const busy = fixture();
    await busy.manager.setPolicy(1, {visible: false, pinned: false});
    const nativeCommand = busy.guest.debugger.sendCommand;
    busy.guest.debugger.sendCommand = async (method, params) => {
        if (method === 'Runtime.evaluate' && params.expression.includes('RTCPeerConnection.prototype')) return {result: {objectId: 'prototype'}};
        if (method === 'Runtime.queryObjects') return {objects: {objectId: 'connections'}};
        if (method === 'Runtime.callFunctionOn') return {result: {value: true}};
        return nativeCommand(method, params);
    };
    await busy.manager.scan(true); assert.equal(busy.manager.stats().frozenTabs, 0);
    busy.guest.debugger.sendCommand = async (method, params) => method === 'Page.getFrameTree' ? {frameTree: {childFrames: [{}]}} : nativeCommand(method, params);
    await busy.manager.scan(true); assert.equal(busy.manager.stats().frozenTabs, 0);
    // A rejected freeze gets a compensating resume and never destroys/recreates a page.
    const failure = fixture();
    await failure.manager.setPolicy(1, {visible: false, pinned: false});
    const failureCommand = failure.guest.debugger.sendCommand;
    failure.guest.debugger.sendCommand = async (method, params) => {
        if (method === 'Page.setWebLifecycleState' && params.state === 'frozen') throw new Error('Unavailable');
        return failureCommand(method, params);
    };
    await failure.manager.scan(true);
    assert.equal(failure.manager.stats().frozenTabs, 0);
    assert(failure.calls.some(([method, params]) => method === 'Page.setWebLifecycleState' && params.state === 'active'));
    const {migrateTabRestoration} = await import('../modules/tabRestoration.js');
    const session = {workspaces: {Work: ['https://old.example']}, tab_sleep_states: {Work: [{url: 'https://new.example/#part', title: 'Draft', scrollY: 50, sessionStorage: '{"draft":"saved"}'}]}};
    const records = migrateTabRestoration(session);
    assert.equal(session.workspaces.Work[0], 'https://new.example/#part');
    assert.equal(records.Work[0].scrollY, 50);
    assert.equal(records.Work[0].sessionStorage, '{"draft":"saved"}');
    assert(!Object.hasOwn(session, 'tab_sleep_states'));
    console.log('Tab suspension policy, lifecycle, races, exemptions, migrations and failure safety passed.');
})().catch(error => {console.error(error); process.exitCode = 1;});
