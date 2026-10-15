// Read the visible surface, including a running clip transition. offsetWidth is
// intentionally constant now and cannot prove that expansion has completed.
module.exports = async function installSidebarGeometry(page) {
    await page.waitForSelector('#Sidebar');
    await page.evaluate(() => {
        window.__sidebarVisibleWidth = () => {
            const sidebar = document.getElementById('Sidebar');
            const clip = getComputedStyle(sidebar).clipPath;
            const right = clip.startsWith('inset(') ? parseFloat(clip.split(' ')[1]) : 0;
            return sidebar.getBoundingClientRect().width - Math.max(0, right || 0);
        };
    });
};
