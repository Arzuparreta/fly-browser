// diagnostics.js
// Privacy-preserving in-memory diagnostics subsystem for Fly Browser
// Retains volatile operational logs in RAM only when explicitly enabled by user

const os = require('os');
const path = require('path');
const { app } = require('electron');

const MAX_LOGS = 100;
let diagnosticsEnabled = false;
const inMemoryLogs = [];

const homeDir = os.homedir();

/**
 * Sanitises strings by stripping home directory, user account names,
 * query parameters, auth tokens, and private IPv4 addresses.
 */
function sanitizeString(str) {
    if (typeof str !== 'string') return str;

    let res = str;

    // Redact detected user home directory
    if (homeDir && homeDir.length > 1) {
        res = res.split(homeDir).join('~');
    }

    // Scrub standard Linux, macOS, and Windows home folder patterns
    res = res.replace(/\/home\/[a-zA-Z0-9._-]+/g, '~');
    res = res.replace(/\/Users\/[a-zA-Z0-9._-]+/g, '~');
    res = res.replace(/[A-Z]:\\Users\\[a-zA-Z0-9._-]+/gi, '~');

    // Redact authentication headers, bearer tokens, API keys, and passwords
    res = res.replace(/(bearer|token|auth|key|secret|password|passwd|api_key|access_token)[=:\s]+[A-Za-z0-9_.\-]{6,}/gi, '$1=[REDACTED]');

    // Strip URL query parameters and fragments to eliminate sensitive tokens
    res = res.replace(/(https?:\/\/[^\s"'<>]+)\?[^\s"'<>#]+/gi, '$1?[REDACTED_QUERY]');
    res = res.replace(/(https?:\/\/[^\s"'<>]+)#[^\s"'<>]+/gi, '$1');

    // Redact RFC 1918 and loopback private IPv4 addresses
    res = res.replace(/\b10\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b192\.168\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b172\.(1[6-9]|2\d|3[0-1])\.\d{1,3}\.\d{1,3}\b/g, '[PRIVATE_IP]');
    res = res.replace(/\b127\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g, '[LOOPBACK_IP]');

    return res;
}

/**
 * Recursively sanitises objects or arrays, omitting sensitive keys.
 */
function sanitizeObject(obj, seen = new WeakSet()) {
    if (obj === null || typeof obj !== 'object') {
        return typeof obj === 'string' ? sanitizeString(obj) : obj;
    }

    if (seen.has(obj)) {
        return '[CIRCULAR]';
    }
    seen.add(obj);

    if (Array.isArray(obj)) {
        return obj.map(item => sanitizeObject(item, seen));
    }

    const forbiddenKeys = new Set(['trusted_domains', 'cookies', 'headers', 'password', 'token', 'auth', 'secret']);
    const clean = {};

    for (const [key, val] of Object.entries(obj)) {
        if (forbiddenKeys.has(key.toLowerCase())) {
            clean[key] = '[REDACTED]';
            continue;
        }
        clean[key] = sanitizeObject(val, seen);
    }

    return clean;
}

/**
 * Initialises diagnostics state from persistent browser configuration.
 */
function initDiagnostics(config) {
    diagnosticsEnabled = !!(config && config.enable_diagnostics);
    if (!diagnosticsEnabled) {
        inMemoryLogs.length = 0;
    }
}

/**
 * Updates diagnostic recording status and purges buffer on deactivation.
 */
function setDiagnosticsEnabled(enabled) {
    diagnosticsEnabled = !!enabled;
    if (!diagnosticsEnabled) {
        inMemoryLogs.length = 0;
    }
}

/**
 * Returns current recording state.
 */
function isDiagnosticsEnabled() {
    return diagnosticsEnabled;
}

/**
 * Appends a sanitised event record to the in-memory ring buffer.
 */
function logEvent(level, category, message, details = null) {
    if (!diagnosticsEnabled) return;

    const entry = {
        timestamp: new Date().toISOString(),
        level: level || 'info',
        category: category ? sanitizeString(String(category)) : 'general',
        message: message ? sanitizeString(String(message)) : '',
        details: details !== null ? sanitizeObject(details) : null
    };

    inMemoryLogs.push(entry);

    if (inMemoryLogs.length > MAX_LOGS) {
        inMemoryLogs.shift();
    }
}

/**
 * Purges the in-memory ring buffer.
 */
function clearLogs() {
    inMemoryLogs.length = 0;
}

/**
 * Returns a cloned snapshot of current in-memory records.
 */
function getLogs() {
    return diagnosticsEnabled ? [...inMemoryLogs] : [];
}

/**
 * Aggregates runtime, hardware, process memory, and sanitised logs.
 */
async function gatherSystemDiagnostics(sessionStats = {}) {
    const cpus = os.cpus() || [];
    const cpuModel = cpus.length > 0 ? cpus[0].model.trim() : 'Unknown';
    const totalMemBytes = os.totalmem();
    const freeMemBytes = os.freemem();
    const usedMemBytes = totalMemBytes - freeMemBytes;

    let processMem = {};
    try {
        processMem = await process.getProcessMemoryInfo();
    } catch (e) {
        processMem = {};
    }

    const memoryUsage = process.memoryUsage();

    let appMetrics = [];
    try {
        appMetrics = app.getAppMetrics().map(m => ({
            pid: m.pid,
            type: m.type,
            cpuPercent: Math.round((m.cpu ? m.cpu.percentCPUUsage : 0) * 10) / 10,
            residentSetKB: m.memory ? (m.memory.workingSetSize ?? m.memory.residentSet ?? 0) : 0
        }));
    } catch (e) {
        appMetrics = [];
    }

    let gpuFeatureStatus = {};
    try {
        gpuFeatureStatus = app.getGPUFeatureStatus();
    } catch (e) {
        gpuFeatureStatus = {};
    }

    let gpuBasicInfo = null;
    try {
        gpuBasicInfo = await app.getGPUInfo('basic');
    } catch (e) {
        gpuBasicInfo = null;
    }

    const desktopSession = process.env.XDG_CURRENT_DESKTOP || process.env.DESKTOP_SESSION || 'Unknown';
    const sessionType = process.env.XDG_SESSION_TYPE || 'Unknown';

    return {
        timestamp: new Date().toISOString(),
        diagnosticsEnabled,
        application: {
            name: 'Fly Browser',
            version: app.getVersion(),
            electron: process.versions.electron,
            chrome: process.versions.chrome,
            node: process.versions.node,
            v8: process.versions.v8
        },
        system: {
            platform: os.platform(),
            arch: os.arch(),
            osRelease: os.release(),
            osType: os.type(),
            desktopSession: sanitizeString(desktopSession),
            sessionType: sanitizeString(sessionType),
            uptimeSeconds: Math.floor(os.uptime())
        },
        hardware: {
            cpuModel: sanitizeString(cpuModel),
            cpuCores: cpus.length,
            totalMemoryMB: Math.round(totalMemBytes / (1024 * 1024)),
            freeMemoryMB: Math.round(freeMemBytes / (1024 * 1024)),
            usedMemoryMB: Math.round(usedMemBytes / (1024 * 1024)),
            usedMemoryPercent: Math.round((usedMemBytes / totalMemBytes) * 100)
        },
        processMemory: {
            residentSetMB: Math.round((processMem.residentSet || 0) / 1024),
            heapTotalMB: Math.round(memoryUsage.heapTotal / (1024 * 1024)),
            heapUsedMB: Math.round(memoryUsage.heapUsed / (1024 * 1024)),
            externalMB: Math.round(memoryUsage.external / (1024 * 1024))
        },
        processes: appMetrics,
        gpu: {
            features: gpuFeatureStatus,
            basic: gpuBasicInfo ? sanitizeObject(gpuBasicInfo) : null
        },
        session: {
            workspaceCount: sessionStats.workspaceCount || 0,
            activeWorkspace: sessionStats.activeWorkspace ? sanitizeString(sessionStats.activeWorkspace) : 'Workspace 1',
            totalTabs: sessionStats.totalTabs || 0,
            activeWebviews: sessionStats.activeWebviews || 0,
            frozenTabs: sessionStats.frozenTabs || 0,
            splitActive: !!sessionStats.splitActive,
            privateBrowsingActive: !!sessionStats.privateBrowsingActive
        },
        logs: diagnosticsEnabled ? [...inMemoryLogs] : []
    };
}

module.exports = {
    initDiagnostics,
    setDiagnosticsEnabled,
    isDiagnosticsEnabled,
    logEvent,
    clearLogs,
    getLogs,
    sanitizeString,
    sanitizeObject,
    gatherSystemDiagnostics
};
