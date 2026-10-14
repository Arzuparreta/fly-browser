// modules/clock.js
// Live clock and date controller for status bar and sidebar widgets

let clockTimer = null;
let currentClockMode = 'datetime'; // 'datetime' | 'time' | 'seconds'
let isClockEnabled = true;

const DAYS_SHORT = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const DAYS_LONG = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS_SHORT = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const MONTHS_LONG = [
    'January', 'February', 'March', 'April', 'May', 'June',
    'July', 'August', 'September', 'October', 'November', 'December'
];

export function getClockData(date = new Date()) {
    const dayIndex = date.getDay();
    const dayNum = date.getDate();
    const monthIndex = date.getMonth();
    const year = date.getFullYear();

    const hours = String(date.getHours()).padStart(2, '0');
    const minutes = String(date.getMinutes()).padStart(2, '0');
    const seconds = String(date.getSeconds()).padStart(2, '0');

    const shortDate = `${DAYS_SHORT[dayIndex]} ${dayNum} ${MONTHS_SHORT[monthIndex]}`;
    const fullDate = `${DAYS_LONG[dayIndex]}, ${dayNum} ${MONTHS_LONG[monthIndex]} ${year}`;
    const shortTime = `${hours}:${minutes}`;
    const fullTime = `${hours}:${minutes}:${seconds}`;

    return {
        shortDate,
        shortTime,
        fullTime,
        tooltip: `${fullDate}, ${fullTime} (Click to switch format)`
    };
}

export function updateClockUI() {
    if (!isClockEnabled) return;
    const data = getClockData();

    const dateText = currentClockMode === 'time' ? '' : data.shortDate;
    const dateDisplay = currentClockMode === 'time' ? 'none' : 'inline-block';
    const timeText = currentClockMode === 'seconds' ? data.fullTime : data.shortTime;

    // Status bar clock elements
    const statusClockEl = document.getElementById('StatusBarClock');
    const statusDateEl = document.getElementById('StatusBarClockDate');
    const statusTimeEl = document.getElementById('StatusBarClockTime');

    if (statusClockEl) {
        if (statusClockEl.title !== data.tooltip) statusClockEl.title = data.tooltip;
        if (statusDateEl) {
            if (statusDateEl.textContent !== dateText) statusDateEl.textContent = dateText;
            if (statusDateEl.style.display !== dateDisplay) statusDateEl.style.display = dateDisplay;
        }
        if (statusTimeEl) {
            if (statusTimeEl.textContent !== timeText) statusTimeEl.textContent = timeText;
        }
    }

    // Sidebar clock elements
    const sidebarClockEl = document.getElementById('SidebarClock');
    const sidebarDateEl = document.getElementById('SidebarClockDate');
    const sidebarTimeEl = document.getElementById('SidebarClockTime');

    if (sidebarClockEl) {
        if (sidebarClockEl.title !== data.tooltip) sidebarClockEl.title = data.tooltip;
        if (sidebarDateEl) {
            if (sidebarDateEl.textContent !== dateText) sidebarDateEl.textContent = dateText;
            if (sidebarDateEl.style.display !== dateDisplay) sidebarDateEl.style.display = dateDisplay;
        }
        if (sidebarTimeEl) {
            if (sidebarTimeEl.textContent !== timeText) sidebarTimeEl.textContent = timeText;
        }
    }
}

export function cycleClockFormat() {
    if (currentClockMode === 'datetime') {
        currentClockMode = 'time';
    } else if (currentClockMode === 'time') {
        currentClockMode = 'seconds';
    } else {
        currentClockMode = 'datetime';
    }

    if (window.miseAPI && typeof window.miseAPI.updateBrowserSettings === 'function') {
        window.miseAPI.updateBrowserSettings({ clock_format: currentClockMode }).catch(() => {});
    }

    updateClockUI();
    return currentClockMode;
}

export function setClockVisibility(visible) {
    isClockEnabled = !!visible;
    const statusClockEl = document.getElementById('StatusBarClock');
    const sidebarClockEl = document.getElementById('SidebarClock');

    if (statusClockEl) {
        statusClockEl.style.display = isClockEnabled ? 'inline-flex' : 'none';
    }
    if (sidebarClockEl) {
        sidebarClockEl.style.display = isClockEnabled ? 'flex' : 'none';
    }

    if (isClockEnabled) updateClockUI();

    const toggleInput = document.getElementById('setting-clock-toggle');
    if (toggleInput) {
        toggleInput.checked = isClockEnabled;
    }

    if (window.miseAPI && typeof window.miseAPI.updateBrowserSettings === 'function') {
        window.miseAPI.updateBrowserSettings({ show_clock: isClockEnabled }).catch(() => {});
    }

    return isClockEnabled;
}

export function toggleClock(force) {
    if (typeof force === 'boolean') {
        return setClockVisibility(force);
    }
    return setClockVisibility(!isClockEnabled);
}

export function initClock() {
    const statusClockEl = document.getElementById('StatusBarClock');
    const sidebarClockEl = document.getElementById('SidebarClock');

    if (statusClockEl) {
        statusClockEl.addEventListener('click', () => {
            cycleClockFormat();
        });
    }

    if (sidebarClockEl) {
        sidebarClockEl.addEventListener('click', () => {
            cycleClockFormat();
        });
    }

    // Load persisted settings
    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        window.miseAPI.getBrowserSettings().then((cfg) => {
            if (cfg) {
                if (typeof cfg.show_clock === 'boolean') {
                    setClockVisibility(cfg.show_clock);
                }
                if (cfg.clock_format && ['datetime', 'time', 'seconds'].includes(cfg.clock_format)) {
                    currentClockMode = cfg.clock_format;
                }
            }
            updateClockUI();
        }).catch(() => {});
    }

    updateClockUI();

    // Start tick aligned to the nearest second
    if (clockTimer) clearInterval(clockTimer);
    const delay = 1000 - (Date.now() % 1000);
    setTimeout(() => {
        updateClockUI();
        clockTimer = setInterval(updateClockUI, 1000);
    }, delay);
}
