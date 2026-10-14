const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'modules/navigation.js'), 'utf8').replace(/^import .*;\n/gm, '').replace(/^export /gm, '');
const { resolveSearchAliasUrl } = require('../modules/overlays/searchEngine.js');
const events = [], address = {style: {display: 'block'}, focus() {}, select() {}};
let selected = {};
const state = {sessionState: {current_workspace: 'A', workspaces: {A: ['old']}}, activeViewsCache: {A: []}};
const view = {setAttribute: (name, value) => events.push(['navigate', name, value])};
state.activeViewsCache.A[0] = view;
const context = {
    resolveSearchAliasUrl, prepareOverlay: () => {}, state, document: {getElementById: id => id === 'WideAddressBar' ? address : null, querySelector: () => selected, querySelectorAll: () => [selected]},
    window: {flyAPI: {saveSession: () => events.push(['save']), getBrowserSettings: async () => ({search_engine: 'https://duckduckgo.com/?q=%s'})}},
    focusActiveWebview: () => {assert.equal(address.style.display, 'none'); events.push(['focus', state.activeViewsCache.A[0]]);},
    wakeTab: () => {state.activeViewsCache.A[0] = view; events.push(['wake']);},
    spawnTabWithUrl: url => {state.sessionState.workspaces.A.push(url); address.style.display = 'none'; events.push(['spawn', url]);},
    isPdfUrl: () => false, evaluateMathExpression: () => null, formatSearchUrl: (query, template) => template.replace('%s', encodeURIComponent(query)),
    requestAnimationFrame: callback => callback()
};
vm.createContext(context); vm.runInContext(source, context);
// Hide suggestions without needing the unrelated suggestion DOM.
context.hideSuggestions = () => events.push(['hide suggestions']);
(async () => {
    await context.handleNavigation('keyboard browser');
    assert.equal(state.sessionState.workspaces.A[0], 'https://duckduckgo.com/?q=keyboard%20browser');
    assert.equal(events.at(-1)[0], 'focus');
    assert(events.some(event => event[0] === 'navigate'));
    assert.strictEqual(events.at(-1)[1], view);
    events.length = 0; delete state.activeViewsCache.A[0]; address.style.display = 'block';
    context.applyTargetUrl('https://example.org');
    assert.deepEqual(events.map(event => event[0]), ['save', 'wake', 'hide suggestions', 'focus']);
    events.length = 0; address.style.display = 'block'; context.displayAddressOverlay();
    assert.deepEqual(events.map(event => event[0]), ['hide suggestions', 'focus']);
    selected = null;
    for (const [input, expected] of [
        ['keyboard browser', 'https://duckduckgo.com/?q=keyboard%20browser'],
        ['example.org', 'https://example.org'],
        ['g keyboard browser', 'https://www.google.com/search?q=keyboard%20browser'],
        ['!w keyboard browser', 'https://duckduckgo.com/?q=!w%20keyboard%20browser']
    ]) {
        events.length = 0;
        state.sessionState.workspaces.A = [];
        address.style.display = 'block';
        await context.handleNavigation(input);
        assert.deepEqual(state.sessionState.workspaces.A, [expected]);
        assert.deepEqual(events.map(event => event[0]), ['hide suggestions', 'spawn', 'focus']);
    }
    console.log('Address navigation and close return focus after hiding the input, including waking tabs.');
})().catch(error => {console.error(error); process.exitCode = 1;});
