// Central state store for browser session, view caches, and overlay states
export const state = {
    sessionState: {
        current_workspace: "Workspace 1",
        workspaces: { "Workspace 1": ["about:blank"] }
    },
    activeViewsCache: {},
    activeTitlesCache: {},
    tabRestoreStates: {},
    tabMediaAudible: {},
    tabAudioMuted: {},
    tabPinned: {},
    tabFavicons: {},
    tabHistory: {},
    splitStates: {},
    
    // UI Active Flags
    dashboardActive: false,
    paletteActive: false,
    preferencesActive: false,
    historyActive: false,
    globalPrivateModeActive: false,
    bookmarksActive: false,
    diagnosticsActive: false,

    // Overlay Data Caches
    dashboardItems: [],
    dashboardSelectionIdx: 0,
    paletteMatches: [],
    paletteSelectionIdx: 0,
    historyResults: [],
    addressSuggestions: [],
    addressSelectionIdx: -1,
    customSearchAliases: {},
    
    // Bookmarks State
    bookmarks: [],
    bookmarkSelectionIdx: 0,
    filteredBookmarksCache: []
};
