#pragma once

#include "qtcore_helpers/qstring_helpers.hpp"

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

#define KEY_LPANEL_PATH QSL("Internal/Core/LPanel/Path")
#define KEY_RPANEL_PATH QSL("Internal/Core/RPanel/Path")
#define KEY_LAST_PATH_FOR_DRIVE_L QSL("Internal/Core/LPanel/LastPathForDrive%1")
#define KEY_LAST_PATH_FOR_DRIVE_R QSL("Internal/Core/RPanel/LastPathForDrive%1")

#define KEY_LAST_COMMANDS_EXECUTED QSL("Internal/Interface/LastCommandsExecuted")

#define KEY_HISTORY_L QSL("Internal/Core/LPanel/History")
#define KEY_HISTORY_R QSL("Internal/Core/RPanel/History")

#define KEY_FAVORITES QSL("Internal/Core/Favorites")

// Copy/move/delete prompt dialog geometry
#define KEY_PROMPT_DIALOG_GEOMETRY QSL("Internal/Interface/PropmptDialog/Geometry")

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
#define KEY_INTERFACE_NUMBERS_AFFTER_LETTERS QSL("Interface/Sorting/NumbersAfterLetters")
#define KEY_INTERFACE_FILE_LIST_FONT QSL("Interface/View/FileListFont")
#define INTERFACE_FILE_LIST_FONT_DEFAULT QSL("Roboto Mono,10,-1,5,25,0,0,0,0,0,Light")
#define KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS QSL("Interface/View/ShowSpecialFolderIcons")
#define KEY_INTERFACE_STYLE_SHEET QSL("Interface/Style/StylesheetText")

// Operations
#define KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION QSL("Operations/CopyMove/AskForConfirmation")

// Editing
#define KEY_EDITOR_PATH QSL("Edit/EditorProgramPath")

// Other
#define KEY_OTHER_SHELL_COMMAND_NAME QSL("Other/Shell/ShellCommandName")
#define KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY QSL("Other/UpdateChecking/CheckAutomatically")
