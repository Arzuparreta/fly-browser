import { state } from './state.js';
import { transferTab } from './tabTransfer.js';
import { renderWorkspaceUI } from './webview.js';
import { focusActiveWebview } from './utils.js';
import { setTargetUrl } from './statusBar.js';

export function openWorkspaceMoveDialog() {
    const source = state.sessionState.current_workspace;
    const index = [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected'));
    const names = Object.keys(state.sessionState.workspaces).filter(name => name !== source);
    if (index < 0) return;
    if (!names.length) { setTargetUrl('Create another workspace before moving a tab', 3000); return; }
    const dialog = document.getElementById('WorkspaceMoveDialog');
    const select = document.getElementById('WorkspaceMoveTarget');
    select.replaceChildren(...names.map(name => { const option = document.createElement('option'); option.value = name; option.textContent = name; return option; }));
    document.getElementById('WorkspaceMoveForm').onsubmit = event => {
        if (event.submitter?.value !== 'move') return;
        event.preventDefault();
        const destination = transferTab(state, source, index, select.value);
        if (destination === null) { dialog.close(); return; }
        state.sessionState.current_workspace = select.value;
        state.dashboardActive = false;
        document.getElementById('DashboardOverlay').style.display = 'none';
        window.flyAPI.saveSession(state.sessionState);
        renderWorkspaceUI(destination);
        dialog.close();
    };
    dialog.onclose = () => { if (!state.preferencesActive) focusActiveWebview(); };
    dialog.showModal(); select.focus();
}
