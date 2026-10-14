const path = require('node:path');
const {Worker} = require('node:worker_threads');
const {ElectronBlocker, adsAndTrackingLists, fetchLists, fetchResources} = require('@ghostery/adblocker-electron');

// Match Ghostery's preset recipe, keeping its fetch/retry policy. Only the CPU-heavy
// parse runs off the main thread; a valid cache never starts this worker.
module.exports = async function buildAdblocker() {
    const [lists, resources] = await Promise.all([
        fetchLists(fetch, adsAndTrackingLists), fetchResources(fetch)
    ]);
    const bytes = await new Promise((resolve, reject) => {
        let result, failure;
        const worker = new Worker(path.join(__dirname, 'adblocker-worker.js'), {workerData: {
            lists, resources,
            engineModulePath: require.resolve('@ghostery/adblocker', {paths: [path.dirname(require.resolve('@ghostery/adblocker-electron'))]})
        }});
        worker.once('message', value => {result = value;});
        worker.once('error', error => {failure = error;});
        worker.once('exit', code => {
            if (failure) reject(failure);
            else if (code === 0 && result instanceof Uint8Array) resolve(result);
            else reject(new Error(`Filter compiler exited without a cache (code ${code}).`));
        });
    });
    return ElectronBlocker.deserialize(bytes);
};
