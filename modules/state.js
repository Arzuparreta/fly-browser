// Central state store for browser session, view caches, and overlay states
export const state = {
    sessionState: {
        current_workspace: "Workspace 1",
        workspaces: { "Workspace 1": ["https://duckduckgo.com"] }
    },
    activeViewsCache: {},
    activeTitlesCache: {},
    tabSleepStates: {},
    tabActivityTimestamps: {},
    tabMediaAudible: {},
    tabAudioMuted: {},
    tabPinned: {},
    tabFavicons: {},
    tabHistory: {},
    tabDarkReaderStates: {},
    splitStates: {},
    
    // UI Active Flags
    dashboardActive: false,
    paletteActive: false,
    preferencesActive: false,
    historyActive: false,
    globalPrivateModeActive: false,
    notesActive: false,
    isEditingNotes: false,
    bookmarksActive: false,
    diagnosticsActive: false,
    awaitingQuickmarkKey: false,
    quickmarkMode: null,

    // Overlay Data Caches
    dashboardItems: [],
    dashboardSelectionIdx: 0,
    paletteMatches: [],
    paletteSelectionIdx: 0,
    historyResults: [],
    addressSuggestions: [],
    addressSelectionIdx: -1,
    
    // Bookmarks and Quickmarks State
    quickmarks: {},
    bookmarks: [],
    bookmarkActiveColumn: 'quickmarks',
    quickmarkSelectionIdx: 0,
    bookmarkSelectionIdx: 0,
    filteredQuickmarksCache: [],
    filteredBookmarksCache: []
};
