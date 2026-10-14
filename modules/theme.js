// modules/theme.js
// Custom visual palette and opacity controller for Fly Browser

export const DEFAULT_THEME_COLORS = {
    dark: {
        accent: '#7aa2f7',
        bg_main: '#1a1b26',
        bg_sidebar: '#16161e',
        text: '#c0caf5',
        sidebar_opacity: 100,
        overlay_opacity: 100
    },
    light: {
        accent: '#2b59c3',
        bg_main: '#e5e5e5',
        bg_sidebar: '#d4d4d4',
        text: '#1a1a1a',
        sidebar_opacity: 100,
        overlay_opacity: 100
    }
};

let cachedThemeConfig = null;
let saveDebounceTimer = null;

function getCurrentThemeMode() {
    return document.body.classList.contains('light-mode') ? 'light' : 'dark';
}

function normalizeHexColor(val, fallback) {
    if (!val || typeof val !== 'string') return fallback;
    const clean = val.trim();
    if (/^#[0-9a-fA-F]{6}$/.test(clean)) return clean.toLowerCase();
    return fallback;
}

export function getActiveThemeColors(cfg, modeOverride = null) {
    const mode = modeOverride || getCurrentThemeMode();
    const defaults = DEFAULT_THEME_COLORS[mode];
    const userTheme = cfg?.theme_colors?.[mode] || {};

    return {
        accent: normalizeHexColor(userTheme.accent, defaults.accent),
        bg_main: normalizeHexColor(userTheme.bg_main, defaults.bg_main),
        bg_sidebar: normalizeHexColor(userTheme.bg_sidebar, defaults.bg_sidebar),
        text: normalizeHexColor(userTheme.text, defaults.text),
        sidebar_opacity: Math.max(40, Math.min(100, parseInt(userTheme.sidebar_opacity ?? defaults.sidebar_opacity, 10))),
        overlay_opacity: Math.max(40, Math.min(100, parseInt(userTheme.overlay_opacity ?? defaults.overlay_opacity, 10)))
    };
}

function setVisualProperty(prop, value) {
    document.documentElement.style.setProperty(prop, value);
    if (document.body) {
        document.body.style.setProperty(prop, value);
    }
}

export function applyThemeVisuals(cfg = null, modeOverride = null) {
    if (cfg) cachedThemeConfig = cfg;
    const mode = modeOverride || getCurrentThemeMode();
    const colors = getActiveThemeColors(cfg || cachedThemeConfig, mode);

    setVisualProperty('--accent', colors.accent);
    setVisualProperty('--bg-main', colors.bg_main);
    setVisualProperty('--bg-sidebar', colors.bg_sidebar);
    setVisualProperty('--text', colors.text);
    setVisualProperty('--sidebar-opacity', `${colors.sidebar_opacity}%`);
    setVisualProperty('--overlay-opacity', `${colors.overlay_opacity}%`);

    highlightActiveSwatch(colors.accent);
}

function highlightActiveSwatch(activeAccent) {
    const swatches = document.querySelectorAll('#accent-preset-swatches .swatch-btn');
    swatches.forEach(btn => {
        const swatchColor = btn.getAttribute('data-color')?.toLowerCase();
        if (swatchColor && swatchColor === activeAccent.toLowerCase()) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
}

function persistThemeConfig(mutator) {
    if (!window.flyAPI || typeof window.flyAPI.getBrowserSettings !== 'function') return;

    if (saveDebounceTimer) clearTimeout(saveDebounceTimer);
    saveDebounceTimer = setTimeout(async () => {
        try {
            const cfg = (await window.flyAPI.getBrowserSettings()) || {};
            if (!cfg.theme_colors) {
                cfg.theme_colors = {
                    dark: { ...DEFAULT_THEME_COLORS.dark },
                    light: { ...DEFAULT_THEME_COLORS.light }
                };
            }
            const mode = getCurrentThemeMode();
            if (!cfg.theme_colors[mode]) {
                cfg.theme_colors[mode] = { ...DEFAULT_THEME_COLORS[mode] };
            }

            mutator(cfg.theme_colors[mode], mode, cfg);
            cachedThemeConfig = cfg;
            await window.flyAPI.updateBrowserSettings(cfg);
        } catch (err) {
            console.error('Failed to persist custom visuals:', err);
        }
    }, 200);
}

export function syncVisualSettingsInputs(cfg = null) {
    if (cfg) cachedThemeConfig = cfg;
    const mode = getCurrentThemeMode();
    const colors = getActiveThemeColors(cfg || cachedThemeConfig, mode);

    const accentPicker = document.getElementById('setting-accent-color-picker');
    const accentText = document.getElementById('setting-accent-color-text');
    if (accentPicker) accentPicker.value = colors.accent;
    if (accentText) accentText.value = colors.accent.toUpperCase();

    const bgSidebarPicker = document.getElementById('setting-bg-sidebar-picker');
    const bgSidebarText = document.getElementById('setting-bg-sidebar-text');
    if (bgSidebarPicker) bgSidebarPicker.value = colors.bg_sidebar;
    if (bgSidebarText) bgSidebarText.value = colors.bg_sidebar.toUpperCase();

    const bgMainPicker = document.getElementById('setting-bg-main-picker');
    const bgMainText = document.getElementById('setting-bg-main-text');
    if (bgMainPicker) bgMainPicker.value = colors.bg_main;
    if (bgMainText) bgMainText.value = colors.bg_main.toUpperCase();

    const textPicker = document.getElementById('setting-text-color-picker');
    const textText = document.getElementById('setting-text-color-text');
    if (textPicker) textPicker.value = colors.text;
    if (textText) textText.value = colors.text.toUpperCase();

    const sidebarSlider = document.getElementById('setting-sidebar-opacity-slider');
    const sidebarVal = document.getElementById('setting-sidebar-opacity-val');
    if (sidebarSlider) sidebarSlider.value = colors.sidebar_opacity;
    if (sidebarVal) sidebarVal.textContent = `${colors.sidebar_opacity}%`;

    const overlaySlider = document.getElementById('setting-overlay-opacity-slider');
    const overlayVal = document.getElementById('setting-overlay-opacity-val');
    if (overlaySlider) overlaySlider.value = colors.overlay_opacity;
    if (overlayVal) overlayVal.textContent = `${colors.overlay_opacity}%`;

    highlightActiveSwatch(colors.accent);
}

export function setupVisualSettingsListeners() {
    function bindColorPair(pickerId, textId, cssVar, configKey) {
        const picker = document.getElementById(pickerId);
        const text = document.getElementById(textId);
        if (!picker || !text) return;

        picker.addEventListener('input', (e) => {
            const hex = e.target.value.toLowerCase();
            text.value = hex.toUpperCase();
            setVisualProperty(cssVar, hex);
            if (configKey === 'accent') highlightActiveSwatch(hex);
            persistThemeConfig((modeConfig) => {
                modeConfig[configKey] = hex;
            });
        });

        text.addEventListener('input', (e) => {
            let val = e.target.value.trim();
            if (!val.startsWith('#')) val = '#' + val;
            if (/^#[0-9a-fA-F]{6}$/.test(val)) {
                picker.value = val;
                setVisualProperty(cssVar, val);
                if (configKey === 'accent') highlightActiveSwatch(val);
                persistThemeConfig((modeConfig) => {
                    modeConfig[configKey] = val.toLowerCase();
                });
            }
        });
    }

    bindColorPair('setting-accent-color-picker', 'setting-accent-color-text', '--accent', 'accent');
    bindColorPair('setting-bg-sidebar-picker', 'setting-bg-sidebar-text', '--bg-sidebar', 'bg_sidebar');
    bindColorPair('setting-bg-main-picker', 'setting-bg-main-text', '--bg-main', 'bg_main');
    bindColorPair('setting-text-color-picker', 'setting-text-color-text', '--text', 'text');

    const swatchesContainer = document.getElementById('accent-preset-swatches');
    if (swatchesContainer) {
        swatchesContainer.addEventListener('click', (e) => {
            const btn = e.target.closest('.swatch-btn');
            if (!btn) return;
            const chosen = btn.getAttribute('data-color');
            if (!chosen) return;

            const accentPicker = document.getElementById('setting-accent-color-picker');
            const accentText = document.getElementById('setting-accent-color-text');
            if (accentPicker) accentPicker.value = chosen;
            if (accentText) accentText.value = chosen.toUpperCase();

            setVisualProperty('--accent', chosen);
            highlightActiveSwatch(chosen);

            persistThemeConfig((modeConfig) => {
                modeConfig.accent = chosen.toLowerCase();
            });
        });
    }

    const sidebarSlider = document.getElementById('setting-sidebar-opacity-slider');
    const sidebarVal = document.getElementById('setting-sidebar-opacity-val');
    if (sidebarSlider) {
        sidebarSlider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value, 10);
            if (sidebarVal) sidebarVal.textContent = `${val}%`;
            setVisualProperty('--sidebar-opacity', `${val}%`);
            persistThemeConfig((modeConfig) => {
                modeConfig.sidebar_opacity = val;
            });
        });
    }

    const overlaySlider = document.getElementById('setting-overlay-opacity-slider');
    const overlayVal = document.getElementById('setting-overlay-opacity-val');
    if (overlaySlider) {
        overlaySlider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value, 10);
            if (overlayVal) overlayVal.textContent = `${val}%`;
            setVisualProperty('--overlay-opacity', `${val}%`);
            persistThemeConfig((modeConfig) => {
                modeConfig.overlay_opacity = val;
            });
        });
    }

    const resetBtn = document.getElementById('setting-reset-theme-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', async () => {
            const mode = getCurrentThemeMode();
            const defaults = DEFAULT_THEME_COLORS[mode];

            setVisualProperty('--accent', defaults.accent);
            setVisualProperty('--bg-main', defaults.bg_main);
            setVisualProperty('--bg-sidebar', defaults.bg_sidebar);
            setVisualProperty('--text', defaults.text);
            setVisualProperty('--sidebar-opacity', `${defaults.sidebar_opacity}%`);
            setVisualProperty('--overlay-opacity', `${defaults.overlay_opacity}%`);

            if (!cachedThemeConfig) {
                cachedThemeConfig = { theme_colors: {} };
            }
            if (!cachedThemeConfig.theme_colors) {
                cachedThemeConfig.theme_colors = {};
            }
            cachedThemeConfig.theme_colors[mode] = { ...defaults };
            syncVisualSettingsInputs(cachedThemeConfig);

            persistThemeConfig((modeConfig) => {
                Object.assign(modeConfig, defaults);
            });
        });
    }
}
