// Compatibility for old sessions only. These records are consumed on first load;
// they never describe a page suspended during the running session.
export function migrateTabRestoration(session) {
    const previous = session.tab_restore_states || session.tab_sleep_states || {};
    const restored = {};
    for (const [workspace, urls] of Object.entries(session.workspaces || {})) {
        restored[workspace] = urls.map((url, index) => {
            const record = previous[workspace]?.[index];
            if (record?.url) urls[index] = record.url;
            return record ? {url: record.url || url, title: record.title,
                scrollX: record.scrollX || 0, scrollY: record.scrollY || 0,
                sessionStorage: record.sessionStorage || null} : null;
        });
    }
    delete session.tab_sleep_states;
    session.tab_restore_states = restored;
    return restored;
}
