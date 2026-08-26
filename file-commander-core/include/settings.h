#pragma once

#include "qtcore_helpers/qstring_helpers.hpp"

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

#define KEY_LPANEL_PATH QSL("Internal/Core/LPanel/Path")
#define KEY_RPANEL_PATH QSL("Internal/Core/RPanel/Path")
#define KEY_LAST_PATH_FOR_DRIVE_L QSL("Internal/Core/LPanel/LastPathForDrive%1")
#define KEY_LAST_PATH_FOR_DRIVE_R QSL("Internal/Core/RPanel/LastPathForDrive%1")

// Per-panel tabs: one list entry per tab, "<id>:<cursorHash>:<path>". Both numbers are decimal, the path is
// everything past the second colon; an entry without that numeric prefix is a pre-id record and is read as a bare
// path. One entry holds a whole tab, so no termination can pair an id with another tab's path.
// The cursor hash is a deterministic function of the item's path, so it survives restarts; 0 means "no cursor".
// (v1 persists only the active tab's history, under KEY_HISTORY_* below; the active tab's path is also mirrored
// to KEY_*PANEL_PATH for back-compat.)
#define KEY_LPANEL_TABS QSL("Internal/Core/LPanel/Tabs")
#define KEY_RPANEL_TABS QSL("Internal/Core/RPanel/Tabs")
#define KEY_LPANEL_ACTIVE_TAB QSL("Internal/Core/LPanel/ActiveTab")
#define KEY_RPANEL_ACTIVE_TAB QSL("Internal/Core/RPanel/ActiveTab")

// Tab ids are handed out from here and never reused: the UI keys its per-tab column state by id, and a reused id
// would attach one tab's columns to another. Must not be recomputed from the ids of the tabs that came back - a
// closed tab's id would be handed out again.
#define KEY_NEXT_TAB_ID QSL("Internal/Core/NextTabId")

// Superseded by the cursor hash inside each KEY_*PANEL_TABS entry: read once to migrate, then removed at shutdown.
#define KEY_LPANEL_TAB_CURSORS QSL("Internal/Core/LPanel/TabCursors")
#define KEY_RPANEL_TAB_CURSORS QSL("Internal/Core/RPanel/TabCursors")

#define KEY_LAST_COMMANDS_EXECUTED QSL("Internal/Interface/LastCommandsExecuted")

#define KEY_HISTORY_L QSL("Internal/Core/LPanel/History")
#define KEY_HISTORY_R QSL("Internal/Core/RPanel/History")

// Per-side, tab-independent log of visited folders (survives tab close/open, unlike KEY_HISTORY_* above
// which is one tab's back/forward chain). Powers the path navigator's quick-revisit dropdown.
#define KEY_LPANEL_VISITED_LOCATIONS QSL("Internal/Core/LPanel/VisitedLocations")
#define KEY_RPANEL_VISITED_LOCATIONS QSL("Internal/Core/RPanel/VisitedLocations")

#define KEY_FAVORITES QSL("Internal/Core/Favorites")

#define KEY_LAST_UPDATE_CHECK_TIMESTAMP QSL("Internal/Interface/Update/LastUpdateCheckTimestamp")

/////////////////////////////////////////////////
// Options accessible via the main UI
/////////////////////////////////////////////////

#define KEY_INTERFACE_SHOW_HIDDEN_FILES QSL("Interface/View/ShowHiddenFiles")

/////////////////////////////////////////////////
// Options accessible via Settings interface
/////////////////////////////////////////////////

// Interface
#define KEY_INTERFACE_RESPECT_LAST_CURSOR_POS QSL("Interface/Selection/RespectLastCursorPosition")
#define KEY_INTERFACE_FILE_LIST_FONT QSL("Interface/View/FileListFont")
#define INTERFACE_FILE_LIST_FONT_DEFAULT QSL("Roboto Mono,10,-1,5,25,0,0,0,0,0,Light")
#define KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS QSL("Interface/View/ShowSpecialFolderIcons")
#define KEY_INTERFACE_STYLE_SHEET QSL("Interface/Style/StylesheetText")
#define KEY_INTERFACE_COLOR_SCHEME QSL("Interface/ColorScheme")
#define KEY_INTERFACE_STYLE QSL("Interface/Style")

// Operations
#define KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION QSL("Operations/CopyMove/AskForConfirmation")

// Editing
#define KEY_EDITOR_PATH QSL("Edit/EditorProgramPath")

// Other
#define KEY_OTHER_SHELL_COMMAND_NAME QSL("Other/Shell/ShellCommandName")
#define KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY QSL("Other/UpdateChecking/CheckAutomatically")
