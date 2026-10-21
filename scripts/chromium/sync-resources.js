#!/usr/bin/env node
// Copies Electron Fly files that native Fly runs unchanged into the native
// overlay (built into <out>/fly_resources). --check fails when a copy is stale.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');
const COPIES = [
    ['modules/readerDistiller.js', 'chromium/overlay/fly/resources/reader/readerDistiller.js'],
    ['assets/identity/icon-128.png', 'chromium/overlay/fly/resources/identity/icon-128.png'],
    ['assets/identity/engraving-chasis@2x.png', 'chromium/overlay/fly/resources/identity/engraving-chasis@2x.png'],
    ['assets/identity/engraving-papel@2x.png', 'chromium/overlay/fly/resources/identity/engraving-papel@2x.png'],
    ['assets/identity/paper-grain.png', 'chromium/overlay/fly/resources/identity/paper-grain.png'],
    // The new tab page's fonts and artwork.
    ['assets/fonts/fonts.css', 'chromium/overlay/fly/resources/new_tab/fonts/fonts.css'],
    ['assets/fonts/doto-0.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/doto-0.woff2'],
    ['assets/fonts/plex-mono-0.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-mono-0.woff2'],
    ['assets/fonts/plex-mono-1.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-mono-1.woff2'],
    ['assets/fonts/plex-mono-2.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-mono-2.woff2'],
    ['assets/fonts/plex-sans-0.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-sans-0.woff2'],
    ['assets/fonts/plex-sans-1.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-sans-1.woff2'],
    ['assets/fonts/plex-sans-2.woff2', 'chromium/overlay/fly/resources/new_tab/fonts/plex-sans-2.woff2'],
    ['assets/identity/memory-chasis@2x.png', 'chromium/overlay/fly/resources/new_tab/identity/memory-chasis@2x.png'],
    ['assets/identity/memory-papel@2x.png', 'chromium/overlay/fly/resources/new_tab/identity/memory-papel@2x.png'],
    ['assets/identity/paper-grain.png', 'chromium/overlay/fly/resources/new_tab/identity/paper-grain.png'],
    ['assets/identity/pixel-32.png', 'chromium/overlay/fly/resources/new_tab/identity/pixel-32.png'],
];
let stale = false;
for (const [from, to] of COPIES) {
    const source = fs.readFileSync(path.join(root, from));
    const target = path.join(root, to);
    if (process.argv.includes('--check')) {
        if (!fs.existsSync(target) || !fs.readFileSync(target).equals(source)) {
            console.error(`${to} differs from ${from}. Run node scripts/chromium/sync-resources.js`);
            stale = true;
        }
    } else {
        fs.mkdirSync(path.dirname(target), {recursive: true});
        fs.writeFileSync(target, source);
    }
}
if (stale) process.exitCode = 1;
