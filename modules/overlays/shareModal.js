import { state } from '../state.js';
import { prepareOverlay } from '../focus.js';
import { getWorkspacePartition, focusActiveWebview, removeWebview } from '../utils.js';

let activeClose = null;

export function closeTransientShareModal() { activeClose?.(); }

export function openTransientShareModal(targetUrl) {
    const overlay = document.getElementById('ShareModalOverlay');
    const container = document.getElementById('ShareModalWebviewContainer');
    if (!overlay || !container || !targetUrl) return;

    closeTransientShareModal();
    prepareOverlay('ShareModalOverlay');
    container.innerHTML = '';

    const webview = document.createElement('webview');
    webview.setAttribute('src', targetUrl);
    webview.setAttribute('allowpopups', 'true');
    webview.style.width = '100%';
    webview.style.height = '100%';
    webview.style.display = 'flex';
    webview.style.flex = '1';

    if (state && state.globalPrivateModeActive) {
        webview.setAttribute('partition', 'FlyPrivateProfile');
    } else {
        const currentWS = state?.sessionState?.current_workspace;
        webview.setAttribute('partition', getWorkspacePartition(currentWS));
    }

    if (window.flyAPI && typeof window.flyAPI.getWebviewPreloadPath === 'function') {
        const preloadPath = window.flyAPI.getWebviewPreloadPath();
        if (preloadPath) {
            webview.setAttribute('preload', preloadPath);
        }
    }

    container.appendChild(webview);
    overlay.style.display = 'flex';

    const closeModal = () => {
        overlay.style.display = 'none';
        removeWebview(webview);
        container.innerHTML = '';
        window.removeEventListener('keydown', handleEsc);
        activeClose = null;
        focusActiveWebview();
    };

    const handleEsc = (e) => {
        if (e.key === 'Escape') {
            e.preventDefault();
            closeModal();
        }
    };

    const closeBtn = document.getElementById('CloseShareModalBtn');
    if (closeBtn) {
        closeBtn.onclick = closeModal;
        closeBtn.focus();
    }

    activeClose = closeModal;
    webview.addEventListener('ipc-message', event => {
        if (event.channel === 'guest-escape') setTimeout(closeModal, 0);
        if (event.channel === 'guest-pointer-enter') document.getElementById('Sidebar').classList.remove('pointer-inside');
    });
    window.addEventListener('keydown', handleEsc);
}
