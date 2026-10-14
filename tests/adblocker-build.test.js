const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {Worker} = require('node:worker_threads');
const {ElectronBlocker, FiltersEngine, Request} = require('@ghostery/adblocker-electron');
const source = fs.readFileSync(path.join(__dirname, '../adblocker-build.js'), 'utf8');
(async () => {
    const lists = ['||ads.example^\n@@||allowed.ads.example^', 'example.org##.advert'];
    let resources = JSON.stringify({redirects: [], scriptlets: []});
    const recipe = [], exits = [];
    const context = {__dirname: path.resolve(__dirname, '..'), Uint8Array, fetch: () => {}, module: {exports: {}},
        require: Object.assign(id => {
            if (id === 'node:worker_threads') return {Worker: class extends Worker {
                constructor(...args) {super(...args); this.on('exit', code => exits.push(code));}
            }};
            if (id === '@ghostery/adblocker-electron') return {ElectronBlocker, adsAndTrackingLists: ['preset'],
                fetchLists: async (_fetch, urls) => {recipe.push(urls); return lists;}, fetchResources: async () => resources};
            return require(id);
        }, {resolve: require.resolve})};
    vm.runInNewContext(source, context);
    const engine = await context.module.exports();
    assert.deepEqual(exits, [0], 'Compiler must have exited before the engine is attached');
    assert.deepEqual(recipe, [['preset']]);
    const expected = FiltersEngine.parse(lists.join('\n')); expected.updateResources(resources, '' + resources.length);
    assert.deepEqual(Buffer.from(engine.serialize()), Buffer.from(expected.serialize()), 'Worker must preserve the preset parse/resources recipe');
    assert(engine.match(Request.fromRawDetails({url: 'https://ads.example/banner.js', type: 'script'})).match);
    assert(!engine.match(Request.fromRawDetails({url: 'https://allowed.ads.example/banner.js', type: 'script'})).match);
    resources = '{';
    await assert.rejects(context.module.exports(), /JSON|Unexpected|property/);
    assert.deepEqual(exits, [0, 1], 'A failed compiler must also exit before rejection');
    resources = JSON.stringify({redirects: [], scriptlets: []});
    await context.module.exports();
    assert.deepEqual(exits, [0, 1, 0], 'A later build can retry after failure');
    console.log('Cold filter worker preserves the preset, transfers its cache and exits before use.');
})().catch(error => {console.error(error); process.exitCode = 1;});
