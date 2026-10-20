#!/usr/bin/env node
// Propagates the declared version into the files that cannot read package.json.
//
// `package.json` is where Fly says what version it is. Electron reports it
// through `app.getVersion()` and electron-builder names every package after
// it, so nothing at runtime has to be told. The copies below are the exceptions:
// npm's lockfile keeps its own record, and the user-script shim is a string
// injected into pages that cannot ask the main process.
//
// Copies are only safe if drift is impossible. `--check` is that guarantee:
// `npm test` runs it on every push and pull request.
//
//   node scripts/version-sync.js              propagate the declared version
//   node scripts/version-sync.js --set X.Y.Z  declare X.Y.Z, then propagate it
//   node scripts/version-sync.js --check      report drift, change nothing
//   node scripts/version-sync.js --print      print the declared version
'use strict';
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const PACKAGE = path.join(ROOT, 'package.json');

// Semantic Versioning 2.0.0 without build metadata, which electron-builder's
// Windows installer cannot express.
const SEMVER = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?$/;

// Each pattern holds the literal text around the version in groups 1 and 3, so
// a replacement never reformats the file.
const TARGETS = [
    {file: 'package-lock.json', json: true},
    {file: 'modules/userScriptShim.js', pattern: /^(        version: ')([^']+)(')$/m},
    {file: 'chromium/overlay/fly/fly_version.h', pattern: /^(inline constexpr char kFlyVersion\[\] = ")([^"]+)(";)$/m}
];

function fail(message) {
    const error = new Error(message);
    error.releaseTooling = true;
    throw error;
}

function declaredVersion(root = ROOT) {
    const version = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')).version;
    if (typeof version !== 'string') fail('package.json: no "version" field');
    return version;
}

function validate(version) {
    if (!SEMVER.test(version)) {
        fail(`${JSON.stringify(version)} is not a version npm and electron-builder both accept.\nExpected MAJOR.MINOR.PATCH with an optional -rc.N suffix.`);
    }
    return version;
}

function setDeclaredVersion(version, root = ROOT) {
    const file = path.join(root, 'package.json');
    const original = fs.readFileSync(file, 'utf8');
    const updated = original.replace(/^(  "version": ")([^"]+)(")/m, `$1${version}$3`);
    if (updated === original) return false;
    fs.writeFileSync(file, updated);
    return true;
}

// The root and `packages[""]` entries are Fly's own version; every other
// "version" in the lockfile belongs to a dependency and must not move.
function lockfileVersions(text) {
    const data = JSON.parse(text);
    return [data.version, data.packages?.['']?.version];
}

function syncLockfile(text, version) {
    const data = JSON.parse(text);
    data.version = version;
    if (data.packages?.['']) data.packages[''].version = version;
    return JSON.stringify(data, null, 2) + '\n';
}

// Copies `version` into every target and returns the ones that disagreed.
function sync(version, {check, root = ROOT} = {}) {
    const stale = [];
    for (const target of TARGETS) {
        const file = path.join(root, target.file);
        const text = fs.readFileSync(file, 'utf8');
        const found = target.json ? lockfileVersions(text) : [text.match(target.pattern)?.[2]];
        if (found.some(value => value === undefined)) {
            fail(`${target.file}: version field not found. The layout changed; update scripts/version-sync.js.`);
        }
        if (found.every(value => value === version)) continue;
        stale.push(`${target.file}: ${found.find(value => value !== version)}`);
        if (check) continue;
        fs.writeFileSync(file, target.json ? syncLockfile(text, version) : text.replace(target.pattern, `$1${version}$3`));
    }
    return stale;
}

function main(argv) {
    if (argv.includes('--print')) {
        console.log(declaredVersion());
        return 0;
    }
    const check = argv.includes('--check');
    const setIndex = argv.indexOf('--set');
    let version;
    if (setIndex !== -1) {
        if (check) fail('--set and --check do the opposite of each other');
        version = validate(argv[setIndex + 1] || '');
        setDeclaredVersion(version);
    } else {
        version = validate(declaredVersion());
    }

    const stale = sync(version, {check});
    if (check) {
        if (stale.length) {
            console.error(`package.json declares ${version}, but:\n${stale.map(line => `  ${line}`).join('\n')}\n\nRun \`node scripts/version-sync.js\` to propagate it.`);
            return 1;
        }
        console.log(`Every copy agrees: ${version}`);
        return 0;
    }
    console.log(stale.length ? `Set to ${version}:\n${stale.map(line => `  ${line.split(':')[0]}`).join('\n')}` : `Already at ${version}; nothing to do.`);
    return 0;
}

if (require.main === module) {
    try {
        process.exitCode = main(process.argv.slice(2));
    } catch (error) {
        if (!error.releaseTooling) throw error;
        console.error(error.message);
        process.exitCode = 1;
    }
}

module.exports = {TARGETS, SEMVER, declaredVersion, setDeclaredVersion, validate, sync, fail};
