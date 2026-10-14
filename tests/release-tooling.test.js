// The release machinery, tested where it can go wrong silently: a build that
// succeeds and ships the wrong number, or a patch release that quietly
// contained a new feature because nobody labelled the pull request.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const versionSync = require('../scripts/version-sync');
const release = require('../scripts/release');

const throws = (fn, pattern) => assert.throws(fn, error => error.releaseTooling && pattern.test(error.message));

// The check CI runs, run locally too, so drift never reaches a push.
const declared = versionSync.declaredVersion();
assert.deepEqual(versionSync.sync(declared, {check: true}), [], 'Run node scripts/version-sync.js');
assert.equal(versionSync.validate(declared), declared);
for (const target of versionSync.TARGETS) assert.ok(fs.existsSync(path.join(__dirname, '..', target.file)), target.file);

for (const version of ['1.0', 'v1.0.0', '1.0.0+build', '01.0.0', '0.1.0-rc.1 x', '']) {
    throws(() => versionSync.validate(version), /not a version/);
}
// Valid semver, but outside the -rc.N convention the release flow reasons about.
for (const version of ['0.1.0-rc', '0.1.0-beta.2', '1.0.0-alpha']) {
    assert.equal(versionSync.validate(version), version);
    throws(() => release.nextVersion(version, 'patch'), /Cannot reason about/);
}

// Below 1.0 a breaking change moves the minor; from 1.0 it moves the major.
assert.equal(release.nextVersion('0.15.0', 'patch'), '0.15.1');
assert.equal(release.nextVersion('0.15.3', 'minor'), '0.16.0');
assert.equal(release.nextVersion('0.15.3', 'major'), '0.16.0');
assert.equal(release.nextVersion('1.4.2', 'major'), '2.0.0');
assert.equal(release.nextVersion('1.4.2', 'minor'), '1.5.0');
throws(() => release.nextVersion('0.15.0', 'none'), /Nothing to release/);
// Candidates: open a line, advance it, then drop the suffix.
assert.equal(release.nextVersion('0.15.0', 'minor', {rc: true}), '0.16.0-rc.1');
assert.equal(release.nextVersion('0.16.0-rc.1', 'patch', {rc: true}), '0.16.0-rc.2');
assert.equal(release.nextVersion('0.16.0-rc.2', 'patch'), '0.16.0');

// Labels: the highest impact wins, unknown impact labels are ignored, and an
// unlabelled pull request counts as a patch but stays recognisable as such.
const pull = (number, ...labels) => release.classify({number, title: `PR ${number}`, labels: labels.map(name => ({name}))});
assert.deepEqual(pull(1, 'impact:minor', 'bug'), {number: 1, title: 'PR 1', impact: 'minor', labelled: true});
assert.equal(pull(2, 'impact:none', 'impact:major').impact, 'major');
assert.deepEqual(pull(3, 'impact:huge'), {number: 3, title: 'PR 3', impact: 'patch', labelled: false});
assert.equal(release.highestImpact([pull(4, 'impact:none'), pull(5, 'impact:patch'), pull(6, 'impact:minor')]), 'minor');
assert.equal(release.highestImpact([pull(7, 'impact:none')]), 'none');
assert.equal(release.highestImpact([pull(8)]), 'patch');

const summary = release.summarise([pull(9, 'impact:minor'), pull(10, 'impact:none'), pull(11)], 'v0.15.0');
assert.match(summary, /since v0\.15\.0/);
assert.ok(summary.indexOf('### impact:minor') < summary.indexOf('### impact:none'), 'Highest impact is listed first');
assert.match(summary, /### No impact label, counted as `patch`\n\n- #11 PR 11/);
assert.match(release.summarise([], null), /since the repository began/);

// Whether a merge already shipped. Long before the tag is decided by time;
// near or after it, by the history, whichever way the clocks disagree.
const taggedAt = new Date('2026-10-14T12:57:06+02:00');
const never = () => assert.fail('History must not be consulted for old merges');
assert.equal(release.inRelease('abc', '2026-10-13T10:00:00Z', 'v1', taggedAt, never), true);
assert.equal(release.inRelease('abc', '2026-10-14T12:55:21Z', 'v1', taggedAt, () => false), false, 'Merged after the tag despite an earlier-looking clock');
assert.equal(release.inRelease('abc', '2026-10-14T10:55:07Z', 'v1', taggedAt, () => true), true, 'The bump merge recorded a second after its tagged commit');
assert.equal(release.inRelease(null, '2026-10-14T10:50:00Z', 'v1', taggedAt, never), true);
assert.equal(release.inRelease('abc', '2026-10-14T11:30:00Z', 'v1', taggedAt, () => null), false, 'Missing commit: the instants decide');

assert.deepEqual(release.parseOptions(['--rc', '--version', '1.0.0']), {rc: true, hold: false, version: '1.0.0'});
throws(() => release.parseOptions(['--version']), /needs a value/);

// Propagation rewrites only Fly's own entries and keeps npm's formatting.
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-version-'));
try {
    const lock = {name: 'fly-browser', version: '0.1.0', lockfileVersion: 3, packages: {'': {name: 'fly-browser', version: '0.1.0'}, 'node_modules/dep': {version: '0.1.0'}}};
    fs.mkdirSync(path.join(temporary, 'modules'));
    fs.writeFileSync(path.join(temporary, 'package.json'), '{\n  "name": "fly-browser",\n  "version": "0.1.0",\n  "dependencies": {\n    "dep": "0.1.0"\n  }\n}\n');
    fs.writeFileSync(path.join(temporary, 'package-lock.json'), JSON.stringify(lock, null, 2) + '\n');
    fs.writeFileSync(path.join(temporary, 'modules/userScriptShim.js'), "    var GM_info = {\n        version: '0.1.0'\n    };\n");

    assert.equal(versionSync.setDeclaredVersion('0.2.0', temporary), true);
    assert.match(fs.readFileSync(path.join(temporary, 'package.json'), 'utf8'), /"version": "0\.2\.0",\n  "dependencies": \{\n    "dep": "0\.1\.0"/);
    assert.equal(versionSync.declaredVersion(temporary), '0.2.0');
    assert.deepEqual(versionSync.sync('0.2.0', {check: true, root: temporary}), ['package-lock.json: 0.1.0', 'modules/userScriptShim.js: 0.1.0']);
    assert.equal(versionSync.sync('0.2.0', {check: true, root: temporary}).length, 2, 'Check mode writes nothing');
    versionSync.sync('0.2.0', {check: false, root: temporary});
    assert.deepEqual(versionSync.sync('0.2.0', {check: true, root: temporary}), []);
    const written = JSON.parse(fs.readFileSync(path.join(temporary, 'package-lock.json'), 'utf8'));
    assert.equal(written.version, '0.2.0');
    assert.equal(written.packages[''].version, '0.2.0');
    assert.equal(written.packages['node_modules/dep'].version, '0.1.0', 'Dependencies keep their versions');
    assert.equal(fs.readFileSync(path.join(temporary, 'modules/userScriptShim.js'), 'utf8'), "    var GM_info = {\n        version: '0.2.0'\n    };\n");

    fs.writeFileSync(path.join(temporary, 'modules/userScriptShim.js'), '// moved\n');
    throws(() => versionSync.sync('0.2.0', {check: true, root: temporary}), /layout changed/);
} finally { fs.rmSync(temporary, {recursive: true, force: true}); }

console.log('Release tooling derives versions from impact labels and keeps every copy of the version in agreement.');
