// Starts Fly with a startup trace (browser + toplevel), then over CDP opens
// new tabs, navigates, switches tabs, and closes tabs. The trace file is
// written when the trace duration ends.
// node trace.mjs <chrome> <workdir> <port>
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [chrome, work, portArg] = process.argv.slice(2);
const port = Number(portArg);
fs.rmSync(work, { recursive: true, force: true });
fs.mkdirSync(path.join(work, 'profile'), { recursive: true });
const traceFile = path.join(work, 'trace.json');
const proc = spawn(chrome, [
  `--user-data-dir=${path.join(work, 'profile')}`,
  `--remote-debugging-port=${port}`, '--no-first-run', '--no-default-browser-check',
  '--password-store=basic', '--window-size=1600,1000',
  '--trace-startup=browser,toplevel,toplevel.flow,views,ui', '--trace-startup-format=json',
  `--trace-startup-file=${traceFile}`, '--trace-startup-duration=75', 'about:blank',
], { env: { ...process.env, DISPLAY: ':93' }, stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let wsUrl;
for (let i = 0; i < 100 && !wsUrl; i++) {
  await sleep(300);
  try { wsUrl = (await (await fetch(`http://127.0.0.1:${port}/json/version`)).json()).webSocketDebuggerUrl; } catch {}
}
const ws = new WebSocket(wsUrl);
await new Promise(r => ws.addEventListener('open', r));
let id = 1; const pending = new Map();
ws.addEventListener('message', e => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); } });
const send = (method, params = {}) => new Promise(r => { const i = id++; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const marks = [];
const mark = name => marks.push({ name, t: Date.now() });
await sleep(4000);
const tabs = [];
mark('new-tabs');
for (let i = 0; i < 10; i++) { tabs.push((await send('Target.createTarget', { url: 'chrome://newtab/' })).targetId); await sleep(1200); }
mark('switch-tabs');
for (let r = 0; r < 3; r++) for (const t of tabs) { await send('Target.activateTarget', { targetId: t }); await sleep(300); }
mark('navigate');
for (const [i, u] of ['https://en.wikipedia.org/wiki/Main_Page', 'https://github.com/trending', 'https://news.ycombinator.com/'].entries()) {
  await send('Target.activateTarget', { targetId: tabs[i] });
  const s = await send('Target.attachToTarget', { targetId: tabs[i], flatten: false });
  await sleep(200);
  await send('Target.sendMessageToTarget', { sessionId: s.sessionId, message: JSON.stringify({ id: 1, method: 'Page.navigate', params: { url: u } }) });
  await sleep(4000);
}
mark('close-tabs');
for (const t of tabs.slice(5)) { await send('Target.closeTarget', { targetId: t }); await sleep(500); }
mark('done');
fs.writeFileSync(path.join(work, 'marks.json'), JSON.stringify(marks));
for (let i = 0; i < 120 && !fs.existsSync(traceFile); i++) await sleep(1000);
await sleep(3000);
await send('Browser.close').catch(() => {});
await sleep(2000);
proc.kill('SIGKILL');
console.log(fs.existsSync(traceFile) ? 'trace written' : 'no trace');
process.exit(0);
