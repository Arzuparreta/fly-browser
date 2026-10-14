// Before configuration version 2, Mise wrote its whole default table to keybinds.json on first launch.
// Regenerate the table of those historical defaults from an upstream ref: node scripts/legacy-keybind-defaults.js upstream/main
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
const keyboard = require('../keyboard');
const root = path.resolve(__dirname, '..');
const file = path.join(root, 'keybinds.js');
const start = '// BEGIN GENERATED LEGACY DEFAULTS\n';
const end = '// END GENERATED LEGACY DEFAULTS\n';
const ref = process.argv[2];
if (!ref) throw new Error('Usage: node scripts/legacy-keybind-defaults.js <upstream ref>');
const git = (...args) => execFileSync('git', args, {cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
const table = {};
const commits = git('log', '--format=%h', ref, '--', 'keybinds.js').trim().split('\n').filter(Boolean);
let versions = 0;
for (const commit of commits) {
    const source = git('show', `${commit}:keybinds.js`);
    // Only versions that seeded keybinds.json with their defaults can have produced a legacy snapshot.
    if (!source.includes('writeFileSync(keybindsPath, JSON.stringify(DEFAULT_KEYBINDS')) continue;
    const sandbox = {module: {exports: {}}, require: name => ({fs: {}, path: {}})[name] ?? {}, console};
    vm.runInNewContext(`${source}\n;module.snapshot = DEFAULT_KEYBINDS;`, sandbox);
    for (const [id, value] of Object.entries(sandbox.module.snapshot)) {
        const values = (Array.isArray(value) ? value : [value]).map(keyboard.normalise);
        if (!values.length || values.some(combo => !combo)) throw new Error(`${commit}: unexpected default for ${id}`);
        (table[id] ??= new Set()).add([...new Set(values)].sort().join(' '));
    }
    versions++;
}
if (!versions) throw new Error(`No legacy keybinds.js versions found in ${ref}`);
const rows = Object.keys(table).sort().map(id => `    ${JSON.stringify(id)}: ${JSON.stringify([...table[id]].sort())}`);
const block = `${start}// Every value that a pre-version-2 Mise seeded into keybinds.json (${versions} versions, generated from ${ref}).\n` +
    `// Each entry is the sorted, space-separated set of normalised chords for that action.\nconst LEGACY_DEFAULTS = {\n${rows.join(',\n')}\n};\n${end}`;
const current = fs.readFileSync(file, 'utf8');
if (!current.includes(start) || !current.includes(end)) throw new Error('keybinds.js lacks the generated legacy defaults markers');
fs.writeFileSync(file, current.slice(0, current.indexOf(start)) + block + current.slice(current.indexOf(end) + end.length));
console.log(`Recorded ${Object.keys(table).length} actions from ${versions} legacy versions.`);
