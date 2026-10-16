const assert = require('node:assert/strict');
const {buildTabContextMenu} = require('../tabContextMenu');

for (const pinned of [false, true]) {
    for (const muted of [false, true]) {
        for (const available of [false, true]) {
            const chosen = [];
            const menu = buildTabContextMenu({pinned, muted, canCloseOthers: available, canCloseRight: available, canReopen: available}, action => chosen.push(action));
            const commands = menu.filter(item => item.type !== 'separator');
            assert.equal(new Set(commands.map(item => item.id)).size, commands.length);
            assert.equal(commands.find(item => item.id === 'close').enabled, !pinned);
            assert.equal(commands.find(item => item.id === 'pin').label, pinned ? 'Unpin' : 'Pin');
            assert.equal(commands.find(item => item.id === 'mute').label, muted ? 'Unmute tab' : 'Mute tab');
            for (const id of ['close-others', 'close-right', 'reopen']) assert.equal(commands.find(item => item.id === id).enabled, available);
            for (const command of commands.filter(item => item.enabled)) command.click();
            assert.deepEqual(chosen, commands.filter(item => item.enabled).map(item => item.id));
        }
    }
}
console.log('Tab menu exposes the available commands, protects pinned tabs and returns the selected action.');
