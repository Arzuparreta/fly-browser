// Each workspace keeps its cookies, storage and logins in its own persistent partition.
// Partitions used to be derived from the workspace name alone, so "Work" and "work", or
// "My web" and "my-web", shared one. The session now records the partition each workspace
// was given (workspace_partitions), and only main hands out new ones.
const fs = require('fs');
const path = require('path');

const PREFIX = 'persist:';
// Electron names a partition's folder after it in lower case, so two partitions that differ
// only in case share a folder. Partitions only use characters that name their folder unchanged.
const PARTITION_PATTERN = /^persist:[a-z0-9_-]+$/;

function slug(name) {
    return String(name).trim().toLowerCase().replace(/[^a-z0-9_-]/gi, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');
}

// The partition earlier versions derived from the name. It holds that workspace's data.
function legacyPartition(name) {
    return PREFIX + (slug(name) || 'default');
}

function unusedPartition(name, isTaken) {
    const base = slug(name).slice(0, 60).replace(/-+$/, '') || 'workspace';
    for (let n = 1; ; n++) {
        const partition = PREFIX + (n === 1 ? base : `${base}-${n}`);
        if (!isTaken(partition)) return partition;
    }
}

// Gives every workspace in the session a partition no other workspace uses. A stored
// partition stays. A workspace without one takes the partition its name used to derive,
// unless another workspace already holds it, and otherwise an unused one.
function assignPartitions(sessionData, isTaken = () => false) {
    const names = Object.keys(sessionData.workspaces || {});
    if (sessionData.current_workspace && !names.includes(sessionData.current_workspace)) names.push(sessionData.current_workspace);
    const stored = sessionData.workspace_partitions && typeof sessionData.workspace_partitions === 'object' ? sessionData.workspace_partitions : {};
    const assigned = {};
    const used = new Set();
    const claim = (name, partition) => {
        if (used.has(partition)) return false;
        assigned[name] = partition;
        used.add(partition);
        return true;
    };
    const unstored = names.filter(name => !(typeof stored[name] === 'string' && PARTITION_PATTERN.test(stored[name]) && claim(name, stored[name])));
    const unclaimed = unstored.filter(name => !claim(name, legacyPartition(name)));
    for (const name of unclaimed) claim(name, unusedPartition(name, partition => used.has(partition) || isTaken(partition)));
    return assigned;
}

// Main keeps the assignment the renderer last saved, so it resolves workspace names to the
// same partitions and never hands out a partition that a workspace, an earlier workspace's
// folder or an earlier allocation in this run has used.
function createWorkspacePartitions(partitionsDir) {
    let current = {};
    const allocated = new Set();
    const isTaken = partition => allocated.has(partition)
        || Object.values(current).includes(partition)
        || fs.existsSync(path.join(partitionsDir, partition.slice(PREFIX.length)));

    return {
        // Completes a loaded session in place and reports whether its partitions changed
        load(sessionData) {
            const before = JSON.stringify(sessionData.workspace_partitions);
            const stored = sessionData.workspace_partitions && typeof sessionData.workspace_partitions === 'object' ? sessionData.workspace_partitions : {};
            sessionData.workspace_partitions = assignPartitions({ ...sessionData, workspace_partitions: { ...current, ...stored } }, isTaken);
            current = { ...sessionData.workspace_partitions };
            return JSON.stringify(sessionData.workspace_partitions) !== before;
        },
        // Follows renames and removals in a session the renderer saves
        save(sessionData) {
            const saved = sessionData.workspace_partitions;
            if (!saved || typeof saved !== 'object') return;
            current = Object.fromEntries(Object.entries(saved).filter(([, partition]) => typeof partition === 'string' && PARTITION_PATTERN.test(partition)));
        },
        get(name) {
            return Object.prototype.hasOwnProperty.call(current, name) ? current[name] : null;
        },
        allocate(name) {
            const partition = unusedPartition(String(name || ''), isTaken);
            allocated.add(partition);
            return partition;
        }
    };
}

module.exports = { assignPartitions, createWorkspacePartitions, legacyPartition };
