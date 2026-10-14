// Fly continues the browser formerly named Mise. Move an existing profile to the new name
// before anything (including the single-instance lock) creates the new directory.
const fs = require('fs');
const path = require('path');

const RENAMES = [['mise-browser', 'fly-browser'], ['Mise Browser', 'Fly Browser']];

function migrateProfile(configRoot) {
    const moved = [];
    for (const [previous, current] of RENAMES) {
        const from = path.join(configRoot, previous);
        const to = path.join(configRoot, current);
        try {
            // Never merge into or overwrite a profile that already exists under the new name.
            if (fs.existsSync(to) || !fs.lstatSync(from, {throwIfNoEntry: false})?.isDirectory()) continue;
            fs.renameSync(from, to);
            moved.push(current);
        } catch (err) {
            console.error(`Could not move the ${previous} profile to ${current}:`, err.message);
        }
    }
    return moved;
}

module.exports = {migrateProfile};
