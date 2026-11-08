// Replays a visit sequence in a disposable Fly profile and records, per
// network request, whether the HTTP disk cache served it and how many bytes
// came from the network. Browser-level CDP with flattened auto-attach, so
// cross-site iframes and workers are counted too.
//
// node replay.mjs <chrome> <workdir> <cacheMiB|default> <visits.json> <passes> <port>
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [chrome, work, sizeArg, visitsFile, passesArg, portArg] = process.argv.slice(2);
const visits = JSON.parse(fs.readFileSync(visitsFile, 'utf8'));
const passes = Number(passesArg);
const port = Number(portArg);
fs.rmSync(work, { recursive: true, force: true });
fs.mkdirSync(path.join(work, 'profile'), { recursive: true });
const args = [
  `--user-data-dir=${path.join(work, 'profile')}`,
  `--disk-cache-dir=${path.join(work, 'cache')}`,
  `--remote-debugging-port=${port}`,
  '--no-first-run', '--no-default-browser-check', '--disable-sync',
  '--password-store=basic', '--window-size=1600,1000', 'about:blank',
];
if (sizeArg !== 'default') args.unshift(`--disk-cache-size=${Number(sizeArg) * 1024 * 1024}`);
const proc = spawn(chrome, args, { env: { ...process.env, DISPLAY: ':93' }, stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));

let wsUrl;
for (let i = 0; i < 100 && !wsUrl; i++) {
  await sleep(300);
  try { wsUrl = (await (await fetch(`http://127.0.0.1:${port}/json/version`)).json()).webSocketDebuggerUrl; } catch {}
}
if (!wsUrl) { console.error('no devtools'); proc.kill('SIGKILL'); process.exit(1); }
const ws = new WebSocket(wsUrl);
await new Promise(r => ws.addEventListener('open', r));
let nextId = 1;
const pending = new Map();
function send(method, params = {}, sessionId) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params, sessionId }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.delete(id)) reject(new Error('timeout ' + method)); }, 20000);
  });
}
const reqs = new Map();
let inflight = 0, lastActivity = Date.now();
// networkBytes: transferred (encoded) bytes. hitBodyBytes and networkBodyBytes:
// decoded body bytes, the only pair that can be compared as a byte hit rate.
const totals = { requests: 0, diskHits: 0, memHits: 0, revalidated304: 0, networkBytes: 0,
                 hitBodyBytes: 0, networkBodyBytes: 0, failed: 0 };
const perPass = [];
let pageSession, loadResolve;
ws.addEventListener('message', async ev => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const p = pending.get(msg.id); pending.delete(msg.id);
    msg.error ? p.reject(new Error(JSON.stringify(msg.error))) : p.resolve(msg.result);
    return;
  }
  const s = msg.sessionId || '';
  const p = msg.params || {};
  switch (msg.method) {
    case 'Target.attachedToTarget': {
      const sid = p.sessionId, type = p.targetInfo.type;
      const tasks = [];
      if (['page', 'iframe', 'worker', 'shared_worker', 'service_worker'].includes(type))
        tasks.push(send('Network.enable', {}, sid).catch(() => {}));
      if (type !== 'worker') tasks.push(send('Target.setAutoAttach',
          { autoAttach: true, waitForDebuggerOnStart: true, flatten: true }, sid).catch(() => {}));
      if (type === 'page' && !pageSession && p.targetInfo.url.startsWith('about:blank')) {
        pageSession = sid;
        tasks.push(send('Page.enable', {}, sid).catch(() => {}));
      }
      await Promise.all(tasks);
      send('Runtime.runIfWaitingForDebugger', {}, sid).catch(() => {});
      break;
    }
    case 'Network.requestWillBeSent':
      if (!/^https?:/.test(p.request.url)) break;
      if (!reqs.has(s + p.requestId)) { inflight++; totals.requests++; }
      reqs.set(s + p.requestId, { disk: false, mem: false, status: 0, bytes: 0 });
      lastActivity = Date.now();
      break;
    case 'Network.requestServedFromCache': {
      const r = reqs.get(s + p.requestId); if (r) r.mem = true; break;
    }
    case 'Network.responseReceived': {
      const r = reqs.get(s + p.requestId);
      if (r) { r.disk = !!p.response.fromDiskCache; r.status = p.response.status; }
      break;
    }
    case 'Network.dataReceived': {
      const r = reqs.get(s + p.requestId); if (r) r.bytes += p.dataLength; break;
    }
    case 'Network.loadingFinished':
    case 'Network.loadingFailed': {
      const r = reqs.get(s + p.requestId);
      if (!r) break;
      reqs.delete(s + p.requestId); inflight--; lastActivity = Date.now();
      if (msg.method === 'Network.loadingFailed') { totals.failed++; break; }
      if (r.disk) { totals.diskHits++; totals.hitBodyBytes += r.bytes; }
      else if (r.mem) totals.memHits++;
      else { totals.networkBytes += p.encodedDataLength || 0; totals.networkBodyBytes += r.bytes; }
      if (r.status === 304) totals.revalidated304++;
      break;
    }
    case 'Page.loadEventFired':
      if (s === pageSession && loadResolve) loadResolve();
      break;
  }
});
await send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: true, flatten: true });
await send('Target.setDiscoverTargets', { discover: true });
for (let i = 0; i < 50 && !pageSession; i++) await sleep(200);
if (!pageSession) { console.error('no page'); proc.kill('SIGKILL'); process.exit(1); }

for (let pass = 0; pass < passes; pass++) {
  const before = { ...totals };
  let loadMs = 0, timeouts = 0;
  for (const url of visits) {
    const start = Date.now();
    const loaded = new Promise(r => { loadResolve = r; });
    try { await send('Page.navigate', { url }, pageSession); } catch { timeouts++; continue; }
    const ok = await Promise.race([loaded.then(() => true), sleep(30000).then(() => false)]);
    if (!ok) { timeouts++; await send('Page.stopLoading', {}, pageSession).catch(() => {}); }
    loadMs += Date.now() - start;
    // Settle: lazy resources, until 1.5 s without network activity (max 10 s).
    const settleStart = Date.now();
    await sleep(1500);
    while (Date.now() - settleStart < 10000 && (inflight > 0 || Date.now() - lastActivity < 1500)) await sleep(250);
  }
  const d = {};
  for (const k of Object.keys(totals)) d[k] = totals[k] - before[k];
  d.loadSeconds = loadMs / 1000; d.loadTimeouts = timeouts; d.pass = pass + 1;
  perPass.push(d);
  console.error(`pass ${pass + 1} done`, JSON.stringify(d));
}
await send('Browser.close').catch(() => {});
await sleep(3000);
proc.kill('SIGKILL');
function du(dir) {
  let n = 0;
  if (!fs.existsSync(dir)) return 0;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    n += e.isDirectory() ? du(p) : fs.statSync(p).size;
  }
  return n;
}
const cacheRoot = path.join(work, 'cache');
const disk = {};
for (const e of fs.existsSync(cacheRoot) ? fs.readdirSync(cacheRoot, { recursive: true }) : [])
  if (/(^|\/)(Cache_Data|js|wasm)$/.test(e)) disk[e] = du(path.join(cacheRoot, e));
const out = { size: sizeArg, perPass, disk };
fs.writeFileSync(path.join(work, 'result.json'), JSON.stringify(out, null, 1));
console.log(JSON.stringify(out));
process.exit(0);
