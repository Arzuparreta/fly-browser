const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {migrateProfile} = require('../profileMigration');

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-profile-migration-'));
try {
    // A development profile and a packaged profile move to their Fly names with their contents.
    fs.mkdirSync(path.join(root, 'mise-browser', 'Partitions', 'work'), {recursive: true});
    fs.writeFileSync(path.join(root, 'mise-browser', 'keybinds.json'), '{}');
    fs.mkdirSync(path.join(root, 'Mise Browser'));
    assert.deepEqual(migrateProfile(root), ['fly-browser', 'Fly Browser']);
    assert.equal(fs.readFileSync(path.join(root, 'fly-browser', 'keybinds.json'), 'utf8'), '{}');
    assert(fs.existsSync(path.join(root, 'fly-browser', 'Partitions', 'work')));
    assert(!fs.existsSync(path.join(root, 'mise-browser')));
    // Running again, or with an existing Fly profile, never merges or overwrites.
    fs.mkdirSync(path.join(root, 'mise-browser'));
    fs.writeFileSync(path.join(root, 'mise-browser', 'keybinds.json'), '{"old": true}');
    assert.deepEqual(migrateProfile(root), []);
    assert.equal(fs.readFileSync(path.join(root, 'fly-browser', 'keybinds.json'), 'utf8'), '{}');
    assert(fs.existsSync(path.join(root, 'mise-browser', 'keybinds.json')));
    // A file with the previous name is not a profile.
    const fileRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-profile-file-'));
    fs.writeFileSync(path.join(fileRoot, 'mise-browser'), '');
    assert.deepEqual(migrateProfile(fileRoot), []);
    fs.rmSync(fileRoot, {recursive: true, force: true});
    assert.deepEqual(migrateProfile(path.join(root, 'missing')), []);
} finally { fs.rmSync(root, {recursive: true, force: true}); }
console.log('Previous Mise profiles move to Fly without merging or overwriting.');
