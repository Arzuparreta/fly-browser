// Drive only Fly's host. Playwright forces focus/visibility on every guest target,
// which prevents Chromium's page lifecycle from behaving as in a normal launch.
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const createFixture = require('./electron-fixture');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
module.exports = async function launch(root, directory) {
    const child = spawn(path.join(root, 'node_modules/electron/dist/electron'), [
        '--no-sandbox', '--disable-gpu', '--ozone-platform=x11', '--remote-debugging-port=0', createFixture(root, directory)
    ], {stdio: ['ignore', 'ignore', 'pipe']});
    let stderr = ''; child.stderr.on('data', data => { stderr += data; });
    let socket;
    try {
        const portFile = path.join(directory, 'data', 'DevToolsActivePort');
        for (let count = 0; !fs.existsSync(portFile); count++) {
            if (count >= 300 || child.exitCode !== null) throw new Error(`Electron failed to start: ${stderr}`);
            await pause(100);
        }
        const [port, endpoint] = fs.readFileSync(portFile, 'utf8').trim().split('\n');
        socket = new WebSocket(`ws://127.0.0.1:${port}${endpoint}`);
        await new Promise((resolve, reject) => {socket.addEventListener('open', resolve, {once: true}); socket.addEventListener('error', reject, {once: true});});
        let id = 0;
        const pending = new Map();
        const errors = [];
        socket.addEventListener('message', event => {
            const response = JSON.parse(event.data);
            if (response.method === 'Runtime.exceptionThrown') errors.push(response.params.exceptionDetails.exception?.description || response.params.exceptionDetails.text);
            const request = pending.get(response.id);
            if (!request) return;
            pending.delete(response.id); clearTimeout(request.timer);
            if (response.error) request.reject(new Error(response.error.message));
            else request.resolve(response.result);
        });
        socket.addEventListener('close', () => {
            for (const request of pending.values()) {clearTimeout(request.timer); request.reject(new Error('Electron closed'));}
            pending.clear();
        });
        const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
            const next = ++id;
            const timer = setTimeout(() => {pending.delete(next); reject(new Error(`${method} timed out`));}, 15000);
            pending.set(next, {resolve, reject, timer});
            socket.send(JSON.stringify({id: next, method, params, sessionId}));
        });
        let host;
        for (let count = 0; !host; count++) {
            host = (await send('Target.getTargets')).targetInfos.find(info => info.url.endsWith('/index.html'));
            if (count >= 300) throw new Error(`No host target: ${stderr}`);
            if (!host) await pause(100);
        }
        const {sessionId} = await send('Target.attachToTarget', {targetId: host.targetId, flatten: true});
        await send('Runtime.enable', {}, sessionId);
        const page = {
            errors,
            async evaluate(fn, argument) {
                const response = await send('Runtime.evaluate', {expression: `(${fn.toString()})(${JSON.stringify(argument) ?? ''})`, awaitPromise: true, returnByValue: true}, sessionId);
                if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text);
                return response.result.value;
            },
            async waitForFunction(fn, argument) {
                for (let count = 0; count < 150; count++) {if (await page.evaluate(fn, argument)) return; await pause(100);}
                throw new Error(`Timed out: ${fn}`);
            },
            waitForTimeout: pause
        };
        return {page, pid: child.pid, async close() {
            socket.close(); child.kill('SIGTERM');
            await new Promise(resolve => {if(child.exitCode !== null) resolve(); else child.once('exit', resolve);});
        }};
    } catch (error) { socket?.close(); child.kill('SIGTERM'); throw error; }
};
