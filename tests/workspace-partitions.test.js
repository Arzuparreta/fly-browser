const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {assignPartitions, createWorkspacePartitions, legacyPartition} = require('../workspacePartitions');

const unique = assigned => new Set(Object.values(assigned)).size === Object.keys(assigned).length;

// A session from before partitions were stored: the first workspace of each name the old
// derivation merged keeps that partition and its logins; the others get their own.
const legacy = {
    current_workspace: 'Gone',
    workspaces: {'Trabajo': [], 'trabajo': [], 'Trabajo 2': [], '日本': [], 'Mi web': [], 'mi-web': []}
};
const migrated = assignPartitions(legacy);
assert.deepEqual(migrated, {
    'Trabajo': 'persist:trabajo',
    'trabajo': 'persist:trabajo--2',
    'Trabajo 2': 'persist:trabajo-2',
    '日本': 'persist:default',
    'Mi web': 'persist:mi-web',
    'mi-web': 'persist:mi-web--2',
    'Gone': 'persist:gone'
});
assert.equal(legacyPartition('Workspace 1'), 'persist:workspace-1');

// Stored partitions stay, even when the name derives another one. A duplicate, a partition
// Electron would fold into another folder and one that is not persistent are replaced.
const stored = assignPartitions({
    current_workspace: 'Oficina',
    workspaces: {'Oficina': [], 'trabajo': [], 'Copia': [], 'Mayus': [], 'Privado': []},
    workspace_partitions: {'Oficina': 'persist:trabajo', 'trabajo': 'persist:otra', 'Copia': 'persist:trabajo', 'Mayus': 'persist:Mayus', 'Privado': 'FlyPrivateProfile'}
});
assert.deepEqual(stored, {
    'Oficina': 'persist:trabajo',
    'trabajo': 'persist:otra',
    'Copia': 'persist:copia',
    'Mayus': 'persist:mayus',
    'Privado': 'persist:privado'
});
assert(unique(migrated) && unique(stored));

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-workspace-partitions-'));
try {
    const unsaved = () => ({current_workspace: 'Trabajo', workspaces: {'Trabajo': [], 'trabajo': []}});
    const partitions = createWorkspacePartitions(root);
    const session = unsaved();
    assert.equal(partitions.load(session), true, 'Completing a session reports the change so it is written');
    assert.deepEqual(session.workspace_partitions, {'Trabajo': 'persist:trabajo', 'trabajo': 'persist:trabajo--2'});
    assert.equal(partitions.load(JSON.parse(JSON.stringify(session))), false);
    assert.equal(partitions.get('trabajo'), 'persist:trabajo--2');
    assert.equal(partitions.get('Unknown'), null);
    // A launch that could not save the assignment, and has since filled its folders, makes
    // the same one again.
    fs.mkdirSync(path.join(root, 'trabajo--2'));
    const relaunched = unsaved();
    createWorkspacePartitions(root).load(relaunched);
    assert.deepEqual(relaunched.workspace_partitions, session.workspace_partitions);

    // New workspaces avoid assigned partitions, folders left by deleted workspaces and
    // earlier allocations.
    fs.mkdirSync(path.join(root, 'trabajo-2'));
    fs.mkdirSync(path.join(root, 'trabajo-3'));
    const first = partitions.allocate('TRABAJO');
    assert.equal(first, 'persist:trabajo-4');
    assert.equal(partitions.allocate('trabajo'), 'persist:trabajo-5');
    assert.equal(partitions.allocate('Nuevo'), 'persist:nuevo');
    assert.equal(partitions.allocate('日本'), 'persist:workspace');
    assert.equal(partitions.allocate('x'.repeat(200)), `persist:${'x'.repeat(60)}`);

    // Saved sessions carry renames and removals; a removed workspace's partition stays taken
    // while its folder exists.
    partitions.save({workspace_partitions: {'Renamed': 'persist:trabajo', 'Bad': 'persist:Bad'}});
    assert.equal(partitions.get('Renamed'), 'persist:trabajo');
    assert.equal(partitions.get('Trabajo'), null);
    assert.equal(partitions.get('Bad'), null);
    assert.equal(partitions.allocate('trabajo'), 'persist:trabajo-6');
} finally { fs.rmSync(root, {recursive: true, force: true}); }
console.log('Workspace partitions are unique, keep existing logins and never reuse an old folder.');
