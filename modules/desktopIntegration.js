// Preferences → About → Desktop integration: the applications menu entry and
// the default browser, both decided by desktopIntegration.js in the main process.

const el = id => document.getElementById(id);
let busy = false;

function entryName(id) {
    return String(id || '').replace(/\.desktop$/, '');
}

function render(status, error) {
    const group = el('DesktopIntegrationGroup');
    if (!group) return;
    if (!status || !status.supported) {
        // Only Linux desktops read these entries.
        group.remove();
        return;
    }
    const entryStatus = el('DesktopEntryStatus');
    const entryButton = el('DesktopEntryBtn');
    const defaultStatus = el('DefaultBrowserStatus');
    const defaultButton = el('DefaultBrowserBtn');

    const entry = status.entry;
    if (entry && entry.ours) {
        entryStatus.textContent = 'Listed in Rofi and your desktop menu. The entry follows this AppImage if you move it or replace it with a newer release.';
        entryButton.textContent = 'Remove from menu';
        entryButton.hidden = false;
    } else if (entry) {
        entryStatus.textContent = `Listed by the entry your installation provides (${entry.path}).`;
        entryButton.hidden = true;
    } else if (status.canInstall) {
        entryStatus.textContent = 'Not listed yet. Fly can add an entry for this AppImage to Rofi and your desktop menu.';
        entryButton.textContent = 'Add to menu';
        entryButton.hidden = false;
    } else {
        entryStatus.textContent = 'No menu entry found. Packages install one; from a source checkout, run npm run install-desktop.';
        entryButton.hidden = true;
    }
    entryButton.disabled = busy;

    if (status.defaultBrowser === undefined) {
        defaultStatus.textContent = 'Install xdg-utils to let Fly read and change the default browser.';
    } else if (status.isDefault) {
        defaultStatus.textContent = 'Fly opens links from other applications.';
    } else if (status.defaultBrowser) {
        defaultStatus.textContent = `Links from other applications open in ${entryName(status.defaultBrowser)}.`;
    } else {
        defaultStatus.textContent = 'No default browser is set.';
    }
    if (!status.isDefault && status.canMakeDefault && !entry) defaultStatus.textContent += ' Making Fly the default also adds it to the menu.';
    defaultButton.hidden = !!status.isDefault || !status.canMakeDefault;
    defaultButton.disabled = busy;

    if (error) entryStatus.textContent = `${error}. ${entryStatus.textContent}`;
}

export async function syncDesktopIntegration() {
    if (!el('DesktopIntegrationGroup') || typeof window.flyAPI?.getDesktopIntegration !== 'function') return;
    render(await window.flyAPI.getDesktopIntegration().catch(() => null));
}

async function act(action) {
    if (busy) return;
    busy = true;
    el('DesktopEntryBtn').disabled = true;
    el('DefaultBrowserBtn').disabled = true;
    let result = null;
    try {
        result = await action();
    } catch (err) {
        result = { ok: false, error: err.message };
    }
    busy = false;
    if (result && result.status) render(result.status, result.ok ? null : result.error);
    else await syncDesktopIntegration();
}

export function setupDesktopIntegrationListeners() {
    const entryButton = el('DesktopEntryBtn');
    const defaultButton = el('DefaultBrowserBtn');
    if (!entryButton || !defaultButton || !window.flyAPI) return;
    entryButton.addEventListener('click', () => act(() => entryButton.textContent === 'Remove from menu'
        ? window.flyAPI.removeFromApplicationsMenu()
        : window.flyAPI.addToApplicationsMenu()));
    defaultButton.addEventListener('click', () => act(() => window.flyAPI.makeDefaultBrowser()));
    syncDesktopIntegration();
}
