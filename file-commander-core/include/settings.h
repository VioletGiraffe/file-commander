#pragma once

// Submodule includes
#include "qtcore_helpers/qstring_helpers.hpp"

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

#define KEY_LPANEL_PATH QSL("Internal/Core/LPanel/Path")
#define KEY_RPANEL_PATH QSL("Internal/Core/RPanel/Path")
#define KEY_LAST_PATH_FOR_DRIVE_L QSL("Internal/Core/LPanel/LastPathForDrive%1")
#define KEY_LAST_PATH_FOR_DRIVE_R QSL("Internal/Core/RPanel/LastPathForDrive%1")

// Per-panel tabs: one list entry per tab, "<id>:<cursorHash>:<sortField>:<sortOrder>:<path>". The numbers are decimal,
// the path is everything past the last of them; an entry without the numeric prefix is a pre-id record and is read
// as a bare path, and one without the sorting fields keeps the default sorting.
// One entry holds a whole tab, so no termination can pair an id with another tab's path.
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

// A QSettings array of the Programs menu entries
#define KEY_USER_PROGRAMS QSL("Programs")

// The file search dialogs of both front ends. A *_HISTORY key holds its field's earlier searches, most recent first.
#define FILE_SEARCH_HISTORY_MAX_ENTRIES 50
#define KEY_FILE_SEARCH_NAME_HISTORY QSL("FileSearchDialog/Ui/NameToFind")
#define KEY_FILE_SEARCH_NAME_CASE_SENSITIVE QSL("FileSearchDialog/Ui/CaseSensitiveName")
#define KEY_FILE_SEARCH_NAME_PARTIAL_MATCH QSL("FileSearchDialog/Ui/NamePartialMatch")
#define FILE_SEARCH_NAME_PARTIAL_MATCH_DEFAULT true
#define KEY_FILE_SEARCH_CONTENTS_HISTORY QSL("FileSearchDialog/Ui/ContentsToFind")
#define KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE QSL("FileSearchDialog/Ui/CaseSensitiveContents")
#define KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS QSL("FileSearchDialog/Ui/ContentsWholeWords")
#define KEY_FILE_SEARCH_CONTENTS_IS_REGEX QSL("FileSearchDialog/Ui/ContentsIsRegex")
#define KEY_FILE_SEARCH_ROOT_HISTORY QSL("FileSearchDialog/Ui/RootFolder")
// The field's text as last left, empty included: a list cleared on purpose must not come back
#define KEY_FILE_SEARCH_EXCLUDED_FOLDERS QSL("FileSearchDialog/Ui/ExcludedFolders")

// The batch rename dialog. A *_HISTORY key holds its field's earlier texts, most recent first.
#define KEY_BATCH_RENAME_NAME_MASK_HISTORY QSL("BatchRenameDialog/Ui/NameMask")
#define KEY_BATCH_RENAME_EXTENSION_MASK_HISTORY QSL("BatchRenameDialog/Ui/ExtensionMask")
#define KEY_BATCH_RENAME_FIND_HISTORY QSL("BatchRenameDialog/Ui/Find")
#define KEY_BATCH_RENAME_REPLACE_WITH_HISTORY QSL("BatchRenameDialog/Ui/ReplaceWith")
#define KEY_BATCH_RENAME_FIND_IS_REGEX QSL("BatchRenameDialog/Ui/FindIsRegex")
#define KEY_BATCH_RENAME_FIND_IS_CASE_SENSITIVE QSL("BatchRenameDialog/Ui/FindIsCaseSensitive")
#define KEY_BATCH_RENAME_COUNTER_START QSL("BatchRenameDialog/Ui/CounterStart")
#define KEY_BATCH_RENAME_COUNTER_STEP QSL("BatchRenameDialog/Ui/CounterStep")
#define KEY_BATCH_RENAME_COUNTER_DIGITS QSL("BatchRenameDialog/Ui/CounterDigits")

/////////////////////////////////////////////////
// Options accessible via Settings interface
/////////////////////////////////////////////////

// Interface
#define KEY_INTERFACE_RESPECT_LAST_CURSOR_POS QSL("Interface/Selection/RespectLastCursorPosition")
#define INTERFACE_RESPECT_LAST_CURSOR_POS_DEFAULT true
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
#define KEY_OTHER_TERMINAL_COMMAND QSL("Other/Shell/TerminalCommand") // Empty: OsShell::defaultTerminalCommand
#define KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY QSL("Other/UpdateChecking/CheckAutomatically")
