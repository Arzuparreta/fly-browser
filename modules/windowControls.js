// modules/windowControls.js
// Window controls manager for classic desktop environments

let isWindowControlsEnabled = false;

export function isWindowControlsVisible() {
    return isWindowControlsEnabled;
}

export function toggleWindowControls(force) {
    const controlsEl = document.getElementById('WindowControls');
    if (!controlsEl) return;

    if (typeof force === 'boolean') {
        isWindowControlsEnabled = force;
    } else {
        isWindowControlsEnabled = !isWindowControlsEnabled;
    }

    controlsEl.style.display = isWindowControlsEnabled ? 'flex' : 'none';

    // Synchronise with settings if API available
    if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
        window.flyAPI.updateBrowserSettings({ show_window_controls: isWindowControlsEnabled }).catch(() => {});
    }

    // Update preferences toggle checkbox if present
    const toggleInput = document.getElementById('setting-window-controls-toggle');
    if (toggleInput) {
        toggleInput.checked = isWindowControlsEnabled;
    }

    return isWindowControlsEnabled;
}

export async function initWindowControls(cfg) {
    const controlsEl = document.getElementById('WindowControls');
    const minBtn = document.getElementById('win-min-btn');
    const maxBtn = document.getElementById('win-max-btn');
    const closeBtn = document.getElementById('win-close-btn');

    if (!controlsEl) return;

    const shouldShow = cfg ? !!cfg.show_window_controls : false;
    toggleWindowControls(shouldShow);

    if (minBtn) {
        minBtn.onclick = () => {
            if (window.flyAPI && typeof window.flyAPI.minimizeWindow === 'function') {
                window.flyAPI.minimizeWindow();
            }
        };
    }

    const updateMaxIcon = (isMaximized) => {
        if (!maxBtn) return;
        const icon = maxBtn.querySelector('i');
        if (isMaximized) {
            if (icon) icon.className = 'fa-regular fa-window-restore';
            maxBtn.title = 'Restore';
        } else {
            if (icon) icon.className = 'fa-regular fa-square';
            maxBtn.title = 'Maximise';
        }
    };

    if (maxBtn) {
        maxBtn.onclick = () => {
            if (window.flyAPI && typeof window.flyAPI.toggleMaximizeWindow === 'function') {
                window.flyAPI.toggleMaximizeWindow();
            }
        };
    }

    if (closeBtn) {
        closeBtn.onclick = () => {
            if (window.flyAPI && typeof window.flyAPI.closeWindow === 'function') {
                window.flyAPI.closeWindow();
            }
        };
    }

    if (window.flyAPI && typeof window.flyAPI.isWindowMaximized === 'function') {
        try {
            const isMax = await window.flyAPI.isWindowMaximized();
            updateMaxIcon(isMax);
        } catch (e) {}
    }

    if (window.flyAPI && typeof window.flyAPI.onWindowMaximizedChange === 'function') {
        window.flyAPI.onWindowMaximizedChange((isMax) => {
            updateMaxIcon(isMax);
        });
    }
}
