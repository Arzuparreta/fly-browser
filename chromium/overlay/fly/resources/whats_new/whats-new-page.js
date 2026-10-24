// Renders the What's New page from whats-new-releases.js, which
// scripts/whats-new.js writes when a release is prepared.
//
// `?from=X.Y.Z` is the version Fly ran before this upgrade: every release
// after it is shown. Without it, only the latest release is.

// Fly closes the tab of a page that calls window.close(). Escape is left to
// whatever already handled it, such as link hints or reader view.
(() => {
    const close = () => window.close();
    for (const button of document.querySelectorAll('[data-close]')) button.addEventListener('click', close);
    document.addEventListener('keydown', event => {
        if (event.key === 'Escape' && !event.defaultPrevented && !event.repeat) close();
    });
})();

(() => {
    const releases = Array.isArray(window.FLY_RELEASES) ? window.FLY_RELEASES : [];
    const releasesRoot = document.getElementById('Releases');
    const summary = document.getElementById('ReleaseSummary');
    const dateLabel = document.getElementById('ReleaseDate');

    // Numeric comparison of X.Y.Z[-rc.N]; a candidate sorts before its release.
    const parts = version => {
        const match = /^(\d+)\.(\d+)\.(\d+)(?:-rc\.(\d+))?$/.exec(String(version || ''));
        return match ? [+match[1], +match[2], +match[3], match[4] === undefined ? Infinity : +match[4]] : null;
    };
    const newer = (a, b) => {
        for (let i = 0; i < 4; i++) if (a[i] !== b[i]) return a[i] > b[i];
        return false;
    };

    const fromVersion = new URLSearchParams(location.search).get('from');
    const from = parts(fromVersion);
    let shown = from ? releases.filter(release => parts(release.version) && newer(parts(release.version), from)) : [];
    if (!shown.length) shown = releases.slice(0, 1);

    // `code` becomes a key and **text** is emphasised; everything else stays text.
    const inline = (parent, text) => {
        for (const piece of String(text).split(/(`[^`]+`|\*\*[^*]+\*\*)/)) {
            if (!piece) continue;
            if (piece.startsWith('`') && piece.endsWith('`') && piece.length > 2) {
                const key = document.createElement('kbd');
                key.textContent = piece.slice(1, -1);
                parent.append(key);
            } else if (piece.startsWith('**') && piece.endsWith('**') && piece.length > 4) {
                const strong = document.createElement('strong');
                strong.textContent = piece.slice(2, -2);
                parent.append(strong);
            } else parent.append(piece);
        }
        return parent;
    };
    const element = (tag, className, text) => {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) inline(node, text);
        return node;
    };

    const card = change => {
        const section = element('section', 'feature-card');
        if (change.impact === 'major') section.append(element('span', 'action-badge', 'Action needed'));
        section.append(element('h2', 'feature-title', change.title));
        for (const text of change.text || []) section.append(element('p', 'feature-desc', text));
        if (change.points?.length) {
            const list = element('ul', 'feature-points');
            for (const point of change.points) list.append(element('li', '', point));
            section.append(list);
        }
        return section;
    };

    const monthYear = date => {
        const parsed = new Date(`${date}T12:00:00`);
        return Number.isNaN(parsed.getTime()) ? '' : parsed.toLocaleDateString('en-GB', {month: 'long', year: 'numeric'});
    };

    if (!shown.length) {
        summary.textContent = 'This build carries no release notes. The full history is on GitHub.';
        return;
    }

    dateLabel.textContent = monthYear(shown[0].date);
    const needsAction = shown.some(release => release.changes.some(change => change.impact === 'major'));
    summary.textContent = (shown.length > 1
        ? `You skipped some releases. Everything since Fly ${fromVersion}, newest first.`
        : `Fly ${shown[0].version}.`) + (needsAction ? ' Read the cards marked Action needed first: they ask something of you.' : '');

    for (const release of shown) {
        if (shown.length > 1) releasesRoot.append(element('h2', 'release-heading', `Fly ${release.version} · ${monthYear(release.date)}`));
        const fixes = [];
        for (const change of release.changes) {
            if (change.impact === 'patch' && !change.points?.length) fixes.push(change.text?.length ? change.text.join(' ') : change.title);
            else releasesRoot.append(card(change));
        }
        if (fixes.length) releasesRoot.append(card({impact: 'patch', title: 'Fixes', text: [], points: fixes}));
    }
})();
