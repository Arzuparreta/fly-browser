// Earlier versions wrote defaults into keybinds.json that the user never chose: pre-version-2 releases
// seeded their whole default table on first launch, and early version 2 saves stored every action.
// Regenerate both tables from history: node scripts/stored-keybind-defaults.js upstream/main personal
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
const keyboard = require('../keyboard');
const root = path.resolve(__dirname, '..');
// legacy: shortcuts seeded by pre-version-2 versions. saved: both scopes from full-table version 2 saves.
// Each entry lists a stored value as sorted normalised chords.
const output = path.join(root, 'stored-keybind-defaults.json');
const refs = process.argv.slice(2);
if (!refs.length) throw new Error('Usage: node scripts/stored-keybind-defaults.js <ref>...');
const git = (...args) => execFileSync('git', args, {cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
const legacy = {};
const saved = {shortcuts: {}, normal: {}};
const record = (table, id, values) => {
    const combos = (Array.isArray(values) ? values : [values]).map(value => value.trim().split(/\s+/).map(keyboard.normalise).join(' '));
    if (combos.some(combo => !combo || combo.split(' ').some(stroke => !stroke))) throw new Error(`Unexpected default for ${id}`);
    (table[id] ??= new Map()).set(JSON.stringify([...new Set(combos)].sort()), true);
};
const counts = {legacy: 0, saved: 0};
for (const commit of git('log', '--format=%h', ...refs, '--', 'keybinds.js').trim().split('\n').filter(Boolean)) {
    const source = git('show', `${commit}:keybinds.js`);
    if (source.includes('writeFileSync(keybindsPath, JSON.stringify(DEFAULT_KEYBINDS')) {
        const sandbox = {module: {exports: {}}, require: () => ({}), console};
        vm.runInNewContext(`${source}\n;module.snapshot = DEFAULT_KEYBINDS;`, sandbox);
        for (const [id, value] of Object.entries(sandbox.module.snapshot)) record(legacy, id, value);
        counts.legacy++;
    } else if (source.includes('JSON.stringify(result.config, null, 2)')) {
        // These versions saved the complete validated table, including every unchanged default.
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-stored-defaults-'));
        try {
            fs.writeFileSync(path.join(directory, 'keybinds.js'), source);
            fs.writeFileSync(path.join(directory, 'keyboard.js'), git('show', `${commit}:keyboard.js`));
            const version = require(path.join(directory, 'keybinds.js'));
            const config = require(path.join(directory, 'keyboard.js')).validateConfig(version.getDefaults(), version.ACTION_METADATA).config;
            for (const scope of ['shortcuts', 'normal']) for (const [id, values] of Object.entries(config[scope])) record(saved[scope], id, values);
        } finally { fs.rmSync(directory, {recursive: true, force: true}); }
        counts.saved++;
    }
}
if (!counts.legacy || !counts.saved) throw new Error(`Expected seeding and full-table versions in ${refs.join(', ')}`);
const rows = (table, indent) => Object.keys(table).sort().map(id => `${indent}${JSON.stringify(id)}: [${[...table[id].keys()].sort().join(', ')}]`).join(',\n');
fs.writeFileSync(output, `{\n  "legacy": {\n${rows(legacy, '    ')}\n  },\n  "saved": {\n    "shortcuts": {\n${rows(saved.shortcuts, '      ')}\n    },\n` +
    `    "normal": {\n${rows(saved.normal, '      ')}\n    }\n  }\n}\n`);
console.log(`Recorded ${counts.legacy} seeding and ${counts.saved} full-table versions.`);
