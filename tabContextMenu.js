// Native tab-strip menu. Actions are returned to the owning renderer, never to a guest.
function buildTabContextMenu(options, choose) {
    const item = (id, label, enabled = true) => ({id, label, enabled, click: () => choose(id)});
    return [
        item('new-tab', 'New tab to the right'),
        {type: 'separator'},
        item('reload', 'Reload'),
        item('duplicate', 'Duplicate'),
        item('pin', options.pinned ? 'Unpin' : 'Pin'),
        item('mute', options.muted ? 'Unmute tab' : 'Mute tab'),
        {type: 'separator'},
        item('close', 'Close', !options.pinned),
        item('close-others', 'Close other tabs', options.canCloseOthers),
        item('close-right', 'Close tabs to the right', options.canCloseRight),
        {type: 'separator'},
        item('reopen', 'Reopen closed tab', options.canReopen)
    ];
}

module.exports = {buildTabContextMenu};
