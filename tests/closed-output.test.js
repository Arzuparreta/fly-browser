const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const path = require('node:path');

// The child keeps logging after its output pipes close, as Fly does after its terminal goes away.
const child = (guarded) => `
    ${guarded ? `require(${JSON.stringify(path.join(__dirname, '..', 'closedOutput.js'))}).ignoreClosedOutput();` : ''}
    process.stdout.write('ready\\n');
    setTimeout(() => {
        console.log('first');
        console.error('second');
        process.emitWarning('late warning');
        setTimeout(() => { console.error('third'); process.stdout.write('fourth\\n'); }, 50);
        setTimeout(() => process.exit(0), 300);
    }, 300);
`;

function runWithClosedOutput(guarded) {
    return new Promise((resolve, reject) => {
        const proc = spawn(process.execPath, ['-e', child(guarded)], {stdio: ['ignore', 'pipe', 'pipe']});
        const timer = setTimeout(() => { proc.kill(); reject(new Error('child did not exit')); }, 10000);
        proc.stdout.once('data', () => { proc.stdout.destroy(); proc.stderr.destroy(); });
        proc.on('exit', (code) => { clearTimeout(timer); resolve(code); });
    });
}

(async () => {
    assert.notEqual(await runWithClosedOutput(false), 0, 'unguarded output should reproduce the uncaught write error');
    assert.equal(await runWithClosedOutput(true), 0);
    console.log('Writes after the terminal or output pipe closes are ignored instead of crashing.');
})().catch((err) => { console.error(err); process.exit(1); });
