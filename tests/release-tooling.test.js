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
assert.deepEqual(pull(1, 'impact:minor', 'bug'), {number: 1, title: 'PR 1', body: '', impact: 'minor', labelled: true});
assert.equal(pull(2, 'impact:none', 'impact:major').impact, 'major');
assert.deepEqual(pull(3, 'impact:huge'), {number: 3, title: 'PR 3', body: '', impact: 'patch', labelled: false});
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
    fs.writeFileSync(path.join(temporary, 'package.json'), '{\n  "name": "fly-browser",\n  "version": "0.1.0",\n  "dependencies": {\n    "dep": "0.1.0"\n  }\n}\n');
    fs.writeFileSync(path.join(temporary, 'package-lock.json'), JSON.stringify(lock, null, 2) + '\n');
    fs.mkdirSync(path.join(temporary, 'chromium/overlay/fly'), {recursive: true});
    fs.writeFileSync(path.join(temporary, 'chromium/overlay/fly/fly_version.h'), 'inline constexpr char kFlyVersion[] = "0.1.0";\n');

    assert.equal(versionSync.setDeclaredVersion('0.2.0', temporary), true);
    assert.match(fs.readFileSync(path.join(temporary, 'package.json'), 'utf8'), /"version": "0\.2\.0",\n  "dependencies": \{\n    "dep": "0\.1\.0"/);
    assert.equal(versionSync.declaredVersion(temporary), '0.2.0');
    assert.deepEqual(versionSync.sync('0.2.0', {check: true, root: temporary}), ['package-lock.json: 0.1.0', 'chromium/overlay/fly/fly_version.h: 0.1.0']);
    assert.equal(versionSync.sync('0.2.0', {check: true, root: temporary}).length, 2, 'Check mode writes nothing');
    versionSync.sync('0.2.0', {check: false, root: temporary});
    assert.deepEqual(versionSync.sync('0.2.0', {check: true, root: temporary}), []);
    const written = JSON.parse(fs.readFileSync(path.join(temporary, 'package-lock.json'), 'utf8'));
    assert.equal(written.version, '0.2.0');
    assert.equal(written.packages[''].version, '0.2.0');
    assert.equal(written.packages['node_modules/dep'].version, '0.1.0', 'Dependencies keep their versions');
    assert.equal(fs.readFileSync(path.join(temporary, 'chromium/overlay/fly/fly_version.h'), 'utf8'), 'inline constexpr char kFlyVersion[] = "0.2.0";\n');

    fs.writeFileSync(path.join(temporary, 'chromium/overlay/fly/fly_version.h'), '// moved\n');
    throws(() => versionSync.sync('0.2.0', {check: true, root: temporary}), /layout changed/);
} finally { fs.rmSync(temporary, {recursive: true, force: true}); }

// What's New is written from the pull requests a release ships.
const whatsNew = require('../scripts/whats-new');
assert.equal(whatsNew.extractNote('## What changed\n\nInternals.'), null, 'The section is optional');
assert.deepEqual(whatsNew.extractNote([
    '## What changed', 'Internal detail.', '',
    "## What's New", '<!-- for users -->', '### Websites follow the interface',
    'Pages with their own dark mode', 'switch between dark and light.', '',
    '- Turn it off in `Preferences` → Appearance.', '- Pages without one', '  are never changed.',
    '## Validation', '- npm test'
].join('\r\n')), {
    title: 'Websites follow the interface',
    text: ['Pages with their own dark mode switch between dark and light.'],
    points: ['Turn it off in `Preferences` → Appearance.', 'Pages without one are never changed.']
});
assert.equal(whatsNew.extractNote('## What’s new\nCurly apostrophe and any case.').text[0], 'Curly apostrophe and any case.');

const shipped = [
    {number: 5, title: 'Fix a crash', impact: 'patch', body: ''},
    {number: 3, title: 'Version 0.1.0', impact: 'none', body: ''},
    {number: 4, title: 'Add a feature', impact: 'minor', body: "## What's New\n- It does a thing."},
    {number: 6, title: 'Remove a workflow', impact: 'major', body: ''}
];
const entry = whatsNew.releaseEntry('0.2.0', '2026-10-15', shipped);
assert.deepEqual(entry.changes.map(change => change.number), [6, 4, 5], 'Most important first, nothing a user cannot see');
assert.deepEqual(entry.changes[1], {impact: 'minor', number: 4, title: 'Add a feature', text: [], points: ['It does a thing.']});

const notes = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-whats-new-'));
try {
    const file = path.join(notes, 'whats-new-releases.js');
    whatsNew.recordRelease('0.2.0-rc.1', [shipped[0]], {date: '2026-10-14', file});
    whatsNew.recordRelease('0.2.0-rc.2', [shipped[2]], {date: '2026-10-15', file});
    whatsNew.recordRelease('0.2.0', [shipped[3]], {date: '2026-10-16', file});
    let releases = whatsNew.readReleases(file);
    assert.deepEqual(releases.map(release => release.version), ['0.2.0'], 'A release absorbs its candidates');
    assert.deepEqual(releases[0].changes.map(change => change.number), [6, 4, 5]);
    whatsNew.recordRelease('0.2.1', [shipped[0]], {date: '2026-10-17', file});
    whatsNew.recordRelease('0.2.1', [shipped[0], shipped[2]], {date: '2026-10-17', file});
    releases = whatsNew.readReleases(file);
    assert.deepEqual(releases.map(release => release.version), ['0.2.1', '0.2.0'], 'Preparing again replaces, newest first');
    assert.equal(releases[0].changes.length, 2);
    fs.writeFileSync(file, 'window.FLY_RELEASES = [];');
    assert.throws(() => whatsNew.readReleases(file), /not written by/);
} finally { fs.rmSync(notes, {recursive: true, force: true}); }

// The bundled history must load and follow the shape the page renders.
for (const release of whatsNew.readReleases()) {
    assert.match(release.version, /^\d+\.\d+\.\d+(-rc\.\d+)?$/);
    assert.match(release.date, /^\d{4}-\d{2}-\d{2}$/);
    for (const change of release.changes) {
        assert.ok(['major', 'minor', 'patch'].includes(change.impact) && change.title && Array.isArray(change.text) && Array.isArray(change.points));
    }
}

console.log("Release tooling derives versions from impact labels, keeps every copy of the version in agreement and writes What's New from what ships.");
