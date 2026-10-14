const {parentPort, workerData} = require('node:worker_threads');
const {FiltersEngine} = require(workerData.engineModulePath);
const engine = FiltersEngine.parse(workerData.lists.join('\n'));
if (workerData.resources !== undefined) engine.updateResources(workerData.resources, '' + workerData.resources.length);
const bytes = engine.serialize();
parentPort.postMessage(bytes, [bytes.buffer]);
parentPort.close();
