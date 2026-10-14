// modules/overlays/diagnostics.js
// Client-side controller for the privacy-first Diagnostics and System Health Overlay
// Designed to keep all diagnostic metrics local, auditable, and off by default

import { state } from '../state.js';
import { focusActiveWebview, escapeHtml } from '../utils.js';

let activeDiagTab = 'system';
let cachedDiagnostics = null;
let currentLogFilter = 'all';
let logSearchQuery = '';

/**
 * Return keyboard focus to active webview when closing overlay.
 */
function returnFocusToWebview() {
    window.miseAllowWebviewFocus = true;
    focusActiveWebview();
}

/**
 * Sanitises strings on the frontend as defense in depth.
 */
function sanitizeFrontendText(text) {
    if (typeof text !== 'string') return text;

    let res = text;
    res = res.replace(/\/home\/[a-zA-Z0-9._-]+/g, '~');
    res = res.replace(/\/Users\/[a-zA-Z0-9._-]+/g, '~');
    res = res.replace(/[A-Z]:\\Users\\[a-zA-Z0-9._-]+/gi, '~');
    res = res.replace(/(bearer|token|auth|key|secret|password|passwd|api_key|access_token)[=:\s]+[A-Za-z0-9_.\-]{6,}/gi, '$1=[REDACTED]');
    res = res.replace(/(https?:\/\/[^\s"'<>]+)\?[^\s"'<>#]+/gi, '$1?[REDACTED_QUERY]');
    res = res.replace(/(https?:\/\/[^\s"'<>]+)#[^\s"'<>]+/gi, '$1');
    res = res.replace(/\b10\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b192\.168\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b172\.(1[6-9]|2\d|3[0-1])\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b127\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '[LOOPBACK_IP]');
    return res;
}

/**
 * Gathers active session counts from browser state.
 */
function gatherCurrentSessionStats() {
    const wsNames = Object.keys(state.sessionState.workspaces || {});
    let totalTabs = 0;
    wsNames.forEach(ws => {
        totalTabs += (state.sessionState.workspaces[ws] || []).length;
    });

    const activeWebviews = document.querySelectorAll('webview').length;
    let hibernatedTabs = 0;
    if (state.tabSleepStates) {
        hibernatedTabs = Object.values(state.tabSleepStates).filter(Boolean).length;
    }

    return {
        workspaceCount: wsNames.length,
        activeWorkspace: state.sessionState.current_workspace || 'Workspace 1',
        totalTabs,
        activeWebviews,
        hibernatedTabs,
        splitActive: !!state.splitStates[state.sessionState.current_workspace]?.isSplit,
        privateBrowsingActive: !!state.globalPrivateModeActive
    };
}

/**
 * Fetches fresh diagnostic metrics from main process.
 */
export async function refreshDiagnosticsData() {
    if (!window.miseAPI || typeof window.miseAPI.getSystemDiagnostics !== 'function') return null;

    try {
        const sessionStats = gatherCurrentSessionStats();
        cachedDiagnostics = await window.miseAPI.getSystemDiagnostics(sessionStats);
        renderDiagnosticsUI(cachedDiagnostics);
        return cachedDiagnostics;
    } catch (err) {
        console.error('Failed to query diagnostics metrics:', err);
        return null;
    }
}

/**
 * Toggles visibility of the Diagnostics modal overlay.
 */
export async function toggleDiagnosticsView() {
    const overlay = document.getElementById('DiagnosticsOverlay');
    if (!overlay) return;

    state.diagnosticsActive = !state.diagnosticsActive;

    if (state.diagnosticsActive) {
        // Dismiss conflicting overlays
        if (state.paletteActive && window.closeCommandPalette) window.closeCommandPalette();
        if (state.dashboardActive && window.toggleDashboardView) window.toggleDashboardView();
        if (state.bookmarksActive && window.toggleBookmarksOverlay) window.toggleBookmarksOverlay();
        if (state.historyActive && window.toggleHistoryOverlay) window.toggleHistoryOverlay();
        if (state.preferencesActive && window.togglePreferencesView) window.togglePreferencesView();

        overlay.style.display = 'flex';
        await refreshDiagnosticsData();
        overlay.focus();
    } else {
        overlay.style.display = 'none';
        returnFocusToWebview();
    }
}

/**
 * Explicitly closes the Diagnostics overlay.
 */
export function closeDiagnosticsView() {
    if (!state.diagnosticsActive) return;
    state.diagnosticsActive = false;
    const overlay = document.getElementById('DiagnosticsOverlay');
    if (overlay) overlay.style.display = 'none';
    returnFocusToWebview();
}

/**
 * Toggles in-memory diagnostic recording status on or off.
 */
export async function toggleDiagnosticsRecording(forcedState = null) {
    if (!window.miseAPI || typeof window.miseAPI.getBrowserSettings !== 'function') return;

    try {
        const cfg = (await window.miseAPI.getBrowserSettings()) || {};
        const nextState = forcedState !== null ? !!forcedState : !cfg.enable_diagnostics;

        cfg.enable_diagnostics = nextState;
        if (typeof window.miseAPI.updateBrowserSettings === 'function') {
            await window.miseAPI.updateBrowserSettings(cfg);
        }
        if (typeof window.miseAPI.setDiagnosticsEnabled === 'function') {
            await window.miseAPI.setDiagnosticsEnabled(nextState);
        }

        // Keep Preferences overlay toggle switch in sync
        const prefToggle = document.getElementById('setting-diagnostics-toggle');
        if (prefToggle) prefToggle.checked = nextState;

        // Keep Diagnostics overlay header toggle in sync
        const headerToggle = document.getElementById('diag-header-recording-toggle');
        if (headerToggle) headerToggle.checked = nextState;

        await refreshDiagnosticsData();
    } catch (err) {
        console.error('Failed to toggle diagnostics recording:', err);
    }
}

/**
 * Master UI renderer for all diagnostic panels.
 */
function renderDiagnosticsUI(data) {
    if (!data) return;

    // Update recording badge in modal header
    const badge = document.getElementById('diag-status-badge');
    const headerToggle = document.getElementById('diag-header-recording-toggle');
    if (badge) {
        if (data.diagnosticsEnabled) {
            badge.className = 'diag-status-badge active';
            badge.innerHTML = '<i class="fa-solid fa-circle-dot"></i> Recording (In-Memory)';
        } else {
            badge.className = 'diag-status-badge inactive';
            badge.innerHTML = '<i class="fa-solid fa-circle-pause"></i> Recording Disabled';
        }
    }
    if (headerToggle) {
        headerToggle.checked = !!data.diagnosticsEnabled;
    }

    renderSystemPane(data);
    renderProcessesPane(data);
    renderLogsPane(data);
    renderReportPane(data);
}

/**
 * Renders the System & Hardware summary tab.
 */
function renderSystemPane(data) {
    const container = document.getElementById('diag-tab-system');
    if (!container) return;

    const app = data.application || {};
    const sys = data.system || {};
    const hw = data.hardware || {};
    const gpu = data.gpu || {};
    const features = gpu.features || {};

    const featureKeys = ['2d_canvas', 'gpu_compositing', 'webgl', 'webgl2', 'video_decode', 'rasterization', 'multiple_raster_threads'];

    const featureRowsHtml = featureKeys.map(k => {
        const val = features[k] || 'unknown';
        const isGood = val === 'enabled';
        const isWarn = val.includes('software') || val.includes('disabled');
        const badgeClass = isGood ? 'diag-pill-ok' : (isWarn ? 'diag-pill-warn' : 'diag-pill-neutral');
        const label = k.replace(/_/g, ' ').toUpperCase();
        return `
            <div class="diag-feature-row">
                <span class="diag-feature-name">${escapeHtml(label)}</span>
                <span class="diag-feature-status ${badgeClass}">${escapeHtml(val)}</span>
            </div>
        `;
    }).join('');

    container.innerHTML = `
        <div class="diag-cards-grid">
            <div class="diag-card">
                <div class="diag-card-title"><i class="fa-solid fa-cube"></i> Application Runtimes</div>
                <div class="diag-kv-list">
                    <div class="diag-kv-row"><span class="diag-k">Mise Browser</span><span class="diag-v">v${escapeHtml(app.version)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Electron Core</span><span class="diag-v">v${escapeHtml(app.electron)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Chromium Engine</span><span class="diag-v">v${escapeHtml(app.chrome)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Node.js Runtime</span><span class="diag-v">v${escapeHtml(app.node)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">V8 Engine</span><span class="diag-v">v${escapeHtml(app.v8)}</span></div>
                </div>
            </div>

            <div class="diag-card">
                <div class="diag-card-title"><i class="fa-solid fa-laptop-code"></i> Operating Environment</div>
                <div class="diag-kv-list">
                    <div class="diag-kv-row"><span class="diag-k">Operating System</span><span class="diag-v">${escapeHtml(sys.platform)} (${escapeHtml(sys.arch)})</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Kernel Release</span><span class="diag-v">${escapeHtml(sys.osRelease)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Desktop Session</span><span class="diag-v">${escapeHtml(sys.desktopSession)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Window Protocol</span><span class="diag-v">${escapeHtml(sys.sessionType)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">System Uptime</span><span class="diag-v">${Math.floor(sys.uptimeSeconds / 3600)}h ${Math.floor((sys.uptimeSeconds % 3600) / 60)}m</span></div>
                </div>
            </div>

            <div class="diag-card">
                <div class="diag-card-title"><i class="fa-solid fa-microchip"></i> Hardware Resources</div>
                <div class="diag-kv-list">
                    <div class="diag-kv-row"><span class="diag-k">Processor Model</span><span class="diag-v">${escapeHtml(hw.cpuModel)}</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Logical Cores</span><span class="diag-v">${hw.cpuCores} threads</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Total Physical RAM</span><span class="diag-v">${hw.totalMemoryMB} MB</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Free Available RAM</span><span class="diag-v">${hw.freeMemoryMB} MB</span></div>
                    <div class="diag-kv-row"><span class="diag-k">Memory Utilisation</span><span class="diag-v">${hw.usedMemoryPercent}% (${hw.usedMemoryMB} MB)</span></div>
                </div>
            </div>

            <div class="diag-card">
                <div class="diag-card-title"><i class="fa-solid fa-wand-magic-sparkles"></i> GPU Acceleration Pipeline</div>
                <div class="diag-feature-table">
                    ${featureRowsHtml}
                </div>
            </div>
        </div>
    `;
}

/**
 * Renders the Process Breakdown & Memory tab.
 */
function renderProcessesPane(data) {
    const container = document.getElementById('diag-tab-processes');
    if (!container) return;

    const pm = data.processMemory || {};
    const procs = data.processes || [];
    const sess = data.session || {};

    let totalRssKB = 0;
    const procTypeMap = {};
    procs.forEach(p => {
        totalRssKB += p.residentSetKB || 0;
        procTypeMap[p.type] = (procTypeMap[p.type] || 0) + 1;
    });

    const rowsHtml = procs.map(p => `
        <tr>
            <td><code>${p.pid}</code></td>
            <td><span class="diag-proc-tag">${escapeHtml(p.type)}</span></td>
            <td>${p.cpuPercent}%</td>
            <td>${Math.round(p.residentSetKB / 1024)} MB</td>
        </tr>
    `).join('');

    container.innerHTML = `
        <div class="diag-proc-summary-cards">
            <div class="diag-mini-stat">
                <span class="diag-stat-label">Active Processes</span>
                <span class="diag-stat-value">${procs.length}</span>
            </div>
            <div class="diag-mini-stat">
                <span class="diag-stat-label">Aggregate Memory (RSS)</span>
                <span class="diag-stat-value">${Math.round(totalRssKB / 1024)} MB</span>
            </div>
            <div class="diag-mini-stat">
                <span class="diag-stat-label">Main Process Heap</span>
                <span class="diag-stat-value">${pm.heapUsedMB || 0} / ${pm.heapTotalMB || 0} MB</span>
            </div>
            <div class="diag-mini-stat">
                <span class="diag-stat-label">Hibernated Tabs</span>
                <span class="diag-stat-value">${sess.hibernatedTabs || 0} / ${sess.totalTabs || 0}</span>
            </div>
        </div>

        <div class="diag-actions-bar">
            <div class="diag-actions-left">
                <span class="diag-hint-text">Workspaces: <strong>${sess.workspaceCount}</strong> | Current: <strong>${escapeHtml(sess.activeWorkspace)}</strong> | Active Frames: <strong>${sess.activeWebviews}</strong></span>
            </div>
            <div class="diag-actions-right">
                <button type="button" id="diag-compact-memory-btn" class="diag-btn-action" title="Trigger V8 Garbage Collection">
                    <i class="fa-solid fa-broom"></i> Compact RAM
                </button>
            </div>
        </div>

        <div class="diag-table-container">
            <table class="diag-table">
                <thead>
                    <tr>
                        <th>PID</th>
                        <th>Subsystem Type</th>
                        <th>CPU</th>
                        <th>Memory (RSS)</th>
                    </tr>
                </thead>
                <tbody>
                    ${rowsHtml.length > 0 ? rowsHtml : '<tr><td colspan="4" style="text-align:center;">No process metrics available.</td></tr>'}
                </tbody>
            </table>
        </div>
    `;

    const compactBtn = document.getElementById('diag-compact-memory-btn');
    if (compactBtn) {
        compactBtn.onclick = async () => {
            if (window.miseAPI && typeof window.miseAPI.compactMemory === 'function') {
                await window.miseAPI.compactMemory();
                compactBtn.innerHTML = '<i class="fa-solid fa-check"></i> Compacted!';
                setTimeout(() => refreshDiagnosticsData(), 800);
            }
        };
    }
}

/**
 * Renders the In-Memory Logs tab.
 */
function renderLogsPane(data) {
    const container = document.getElementById('diag-tab-logs');
    if (!container) return;

    const enabled = !!data.diagnosticsEnabled;
    const rawLogs = data.logs || [];

    const filteredLogs = rawLogs.filter(item => {
        if (currentLogFilter !== 'all' && item.level !== currentLogFilter) return false;
        if (logSearchQuery) {
            const q = logSearchQuery.toLowerCase();
            const msg = (item.message || '').toLowerCase();
            const cat = (item.category || '').toLowerCase();
            if (!msg.includes(q) && !cat.includes(q)) return false;
        }
        return true;
    });

    const logsHtml = filteredLogs.map(item => {
        const time = item.timestamp ? item.timestamp.split('T')[1].replace('Z', '') : '';
        const levelClass = item.level === 'error' ? 'log-badge-error' : (item.level === 'warn' ? 'log-badge-warn' : 'log-badge-info');
        return `
            <div class="diag-log-row">
                <span class="diag-log-time">${escapeHtml(time)}</span>
                <span class="diag-log-level ${levelClass}">${escapeHtml(item.level.toUpperCase())}</span>
                <span class="diag-log-cat">[${escapeHtml(item.category)}]</span>
                <span class="diag-log-msg">${escapeHtml(sanitizeFrontendText(item.message))}</span>
            </div>
        `;
    }).join('');

    container.innerHTML = `
        <div class="diag-logs-header">
            <div class="diag-logs-filters">
                <button type="button" class="diag-filter-chip ${currentLogFilter === 'all' ? 'active' : ''}" data-filter="all">All (${rawLogs.length})</button>
                <button type="button" class="diag-filter-chip ${currentLogFilter === 'error' ? 'active' : ''}" data-filter="error">Errors</button>
                <button type="button" class="diag-filter-chip ${currentLogFilter === 'warn' ? 'active' : ''}" data-filter="warn">Warnings</button>
                <button type="button" class="diag-filter-chip ${currentLogFilter === 'info' ? 'active' : ''}" data-filter="info">Info</button>
            </div>
            <div class="diag-logs-controls">
                <input type="text" id="diag-log-search" class="diag-search-input" placeholder="Search log entries..." value="${escapeHtml(logSearchQuery)}">
                <button type="button" id="diag-clear-logs-btn" class="diag-btn-action" title="Clear in-memory ring buffer">
                    <i class="fa-solid fa-trash-can"></i> Clear Logs
                </button>
            </div>
        </div>

        ${!enabled ? `
            <div class="diag-empty-logs-warning">
                <i class="fa-solid fa-shield-halved" style="font-size: 32px; color: var(--accent); margin-bottom: 8px;"></i>
                <h3>Diagnostic Recording is Currently Disabled</h3>
                <p>Mise Browser enforces strict zero-telemetry by default. Enable recording to capture runtime errors and warnings in volatile RAM.</p>
                <button type="button" id="diag-enable-now-btn" class="diag-btn-action primary" style="margin-top: 10px;">
                    Enable Diagnostics Recording
                </button>
            </div>
        ` : (filteredLogs.length === 0 ? `
            <div class="diag-empty-logs">
                <i class="fa-solid fa-check-circle" style="font-size: 28px; opacity: 0.5; margin-bottom: 8px;"></i>
                <p>No logged events match the active criteria. In-memory buffer is healthy.</p>
            </div>
        ` : `
            <div class="diag-logs-list">
                ${logsHtml}
            </div>
        `)}
        
        <div class="diag-logs-footer-note">
            <i class="fa-solid fa-lock"></i> Operational logs are stored exclusively in volatile memory and purged when quitting. Nothing is ever saved to disk or transmitted across the network.
        </div>
    `;

    // Bind log filter chips
    container.querySelectorAll('.diag-filter-chip').forEach(btn => {
        btn.onclick = () => {
            currentLogFilter = btn.getAttribute('data-filter') || 'all';
            renderLogsPane(data);
        };
    });

    const searchInput = document.getElementById('diag-log-search');
    if (searchInput) {
        searchInput.oninput = (e) => {
            logSearchQuery = e.target.value.trim();
            renderLogsPane(data);
        };
    }

    const clearBtn = document.getElementById('diag-clear-logs-btn');
    if (clearBtn) {
        clearBtn.onclick = async () => {
            if (window.miseAPI && typeof window.miseAPI.clearDiagnosticsLogs === 'function') {
                await window.miseAPI.clearDiagnosticsLogs();
                await refreshDiagnosticsData();
            }
        };
    }

    const enableNowBtn = document.getElementById('diag-enable-now-btn');
    if (enableNowBtn) {
        enableNowBtn.onclick = async () => {
            await toggleDiagnosticsRecording(true);
        };
    }
}

/**
 * Builds the sanitised markdown report suitable for GitHub issue submissions.
 */
function buildMarkdownIssueReport(data) {
    if (!data) return 'No diagnostics data collected.';

    const app = data.application || {};
    const sys = data.system || {};
    const hw = data.hardware || {};
    const gpu = data.gpu?.features || {};
    const sess = data.session || {};
    const procs = data.processes || [];
    const logs = data.logs || [];

    const lines = [];
    lines.push('### Mise Browser Diagnostic Report');
    lines.push(`_Generated: ${data.timestamp || new Date().toISOString()}_`);
    lines.push('_Privacy status: 100% local, scrubbed of user paths, auth tokens, and private IPs._\n');

    lines.push('#### 1. Software Environment');
    lines.push(`- **Mise Version**: v${app.version || '0.15.0'}`);
    lines.push(`- **Electron**: v${app.electron || 'unknown'}`);
    lines.push(`- **Chromium**: v${app.chrome || 'unknown'}`);
    lines.push(`- **Node.js**: v${app.node || 'unknown'}`);
    lines.push(`- **OS**: ${sys.platform} (${sys.arch}) - Kernel ${sys.osRelease}`);
    lines.push(`- **Desktop / Protocol**: ${sys.desktopSession} (${sys.sessionType})\n`);

    lines.push('#### 2. Hardware & GPU Pipeline');
    lines.push(`- **CPU**: ${hw.cpuModel} (${hw.cpuCores} cores)`);
    lines.push(`- **Physical Memory**: Total ${hw.totalMemoryMB} MB | Free ${hw.freeMemoryMB} MB | Utilisation ${hw.usedMemoryPercent}%`);
    lines.push(`- **2D Canvas**: ${gpu['2d_canvas'] || 'unknown'}`);
    lines.push(`- **GPU Compositing**: ${gpu['gpu_compositing'] || 'unknown'}`);
    lines.push(`- **WebGL**: ${gpu['webgl'] || 'unknown'}`);
    lines.push(`- **Video Decode**: ${gpu['video_decode'] || 'unknown'}`);
    lines.push(`- **Rasterization**: ${gpu['rasterization'] || 'unknown'}\n`);

    lines.push('#### 3. Session & Process State');
    lines.push(`- **Workspaces**: ${sess.workspaceCount}`);
    lines.push(`- **Total Tabs**: ${sess.totalTabs} (Hibernated: ${sess.hibernatedTabs}, Active Webviews: ${sess.activeWebviews})`);
    lines.push(`- **Total Chromium Processes**: ${procs.length}`);
    lines.push(`- **Main Process Heap**: ${data.processMemory?.heapUsedMB || 0} MB / ${data.processMemory?.heapTotalMB || 0} MB\n`);

    if (logs.length > 0) {
        lines.push('#### 4. Recent Sanitised Operational Logs');
        lines.push('<details>');
        lines.push(`<summary>Expand ${logs.length} in-memory event records</summary>\n`);
        lines.push('```text');
        logs.slice(-50).forEach(l => {
            const time = l.timestamp ? l.timestamp.split('T')[1].replace('Z', '') : '';
            lines.push(`[${time}] [${l.level.toUpperCase()}] [${l.category}] ${sanitizeFrontendText(l.message)}`);
        });
        lines.push('```');
        lines.push('</details>\n');
    } else {
        lines.push('#### 4. Operational Logs');
        lines.push('_No errors or warnings recorded in memory._\n');
    }

    return lines.join('\n');
}

/**
 * Renders the Bug Report Generator & Export tab.
 */
function renderReportPane(data) {
    const container = document.getElementById('diag-tab-report');
    if (!container) return;

    const mdReport = buildMarkdownIssueReport(data);

    container.innerHTML = `
        <div class="diag-report-banner">
            <div class="diag-report-banner-text">
                <h3><i class="fa-solid fa-file-shield"></i> Sanitised Bug Report Generator</h3>
                <p>Copy this report directly into a GitHub Issue. All file paths, usernames, query strings, and local IPs have been pre-scrubbed.</p>
            </div>
            <div class="diag-report-actions">
                <button type="button" id="diag-copy-report-btn" class="diag-btn-action primary">
                    <i class="fa-solid fa-copy"></i> Copy Markdown Report
                </button>
                <button type="button" id="diag-export-json-btn" class="diag-btn-action">
                    <i class="fa-solid fa-download"></i> Export JSON
                </button>
                <button type="button" id="diag-open-github-btn" class="diag-btn-action">
                    <i class="fa-brands fa-github"></i> Open Issues Page
                </button>
            </div>
        </div>

        <div class="diag-markdown-preview-card">
            <pre class="diag-markdown-pre"><code>${escapeHtml(mdReport)}</code></pre>
        </div>
    `;

    const copyBtn = document.getElementById('diag-copy-report-btn');
    if (copyBtn) {
        copyBtn.onclick = async () => {
            try {
                await navigator.clipboard.writeText(mdReport);
                copyBtn.innerHTML = '<i class="fa-solid fa-check"></i> Copied to Clipboard!';
                copyBtn.classList.add('success');
                setTimeout(() => {
                    copyBtn.innerHTML = '<i class="fa-solid fa-copy"></i> Copy Markdown Report';
                    copyBtn.classList.remove('success');
                }, 2000);
            } catch (err) {
                console.error('Failed to copy to clipboard:', err);
            }
        };
    }

    const exportBtn = document.getElementById('diag-export-json-btn');
    if (exportBtn) {
        exportBtn.onclick = () => {
            try {
                const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `mise-diagnostics-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
            } catch (err) {
                console.error('Failed to export JSON report:', err);
            }
        };
    }

    const githubBtn = document.getElementById('diag-open-github-btn');
    if (githubBtn) {
        githubBtn.onclick = () => {
            const url = 'https://github.com/rakosn1cek/mise-browser/issues/new';
            if (window.miseAPI && typeof window.miseAPI.openExternal === 'function') {
                window.miseAPI.openExternal(url);
            } else if (window.spawnTabWithUrl) {
                window.spawnTabWithUrl(url);
            }
        };
    }
}

/**
 * Wires up global event listeners for the Diagnostics overlay.
 */
export function setupDiagnosticsListeners() {
    const overlay = document.getElementById('DiagnosticsOverlay');
    if (!overlay) return;

    // Handle Escape key to close modal
    overlay.onkeydown = (e) => {
        if (e.key === 'Escape') {
            e.preventDefault();
            toggleDiagnosticsView();
        }
    };

    // Close button
    const closeBtn = document.getElementById('CloseDiagnosticsBtn');
    if (closeBtn) {
        closeBtn.onclick = (e) => {
            e.preventDefault();
            toggleDiagnosticsView();
        };
    }

    // Refresh button
    const refreshBtn = document.getElementById('diag-refresh-btn');
    if (refreshBtn) {
        refreshBtn.onclick = async (e) => {
            e.preventDefault();
            refreshBtn.classList.add('spinning');
            await refreshDiagnosticsData();
            setTimeout(() => refreshBtn.classList.remove('spinning'), 500);
        };
    }

    // Header toggle switch for recording
    const headerToggle = document.getElementById('diag-header-recording-toggle');
    if (headerToggle) {
        headerToggle.onchange = async () => {
            await toggleDiagnosticsRecording(headerToggle.checked);
        };
    }

    // Tab button navigation
    const tabBtns = overlay.querySelectorAll('.diag-nav-tab');
    tabBtns.forEach(btn => {
        btn.onclick = () => {
            tabBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');

            const targetTab = btn.getAttribute('data-tab');
            activeDiagTab = targetTab;

            overlay.querySelectorAll('.diag-tab-content').forEach(pane => {
                pane.style.display = (pane.id === `diag-tab-${targetTab}`) ? 'block' : 'none';
            });
        };
    });
}
