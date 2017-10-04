#pragma once

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

#define KEY_LPANEL_PATH "Internal/Core/LPanel/Path"
#define KEY_RPANEL_PATH "Internal/Core/RPanel/Path"
#define KEY_LAST_PATH_FOR_DRIVE_L QStringLiteral("Internal/Core/LPanel/LastPathForDrive%1")
#define KEY_LAST_PATH_FOR_DRIVE_R QStringLiteral("Internal/Core/RPanel/LastPathForDrive%1")

#define KEY_LAST_COMMANDS_EXECUTED "Internal/Interface/LastCommandsExecuted"

#define KEY_HISTORY_L "Internal/Core/LPanel/History"
#define KEY_HISTORY_R "Internal/Core/RPanel/History"

#define KEY_FAVORITES "Internal/Core/Favorites"

// Copy/move/delete prompt dialog geometry
#define KEY_PROMPT_DIALOG_GEOMETRY "Internal/Interface/PropmptDialog/Geometry"

#define KEY_LAST_UPDATE_CHECK_TIMESTAMP "Internal/Interface/Update/LastUpdateCheckTimestamp"

/////////////////////////////////////////////////
// Options accessible via the main UI
/////////////////////////////////////////////////

#define KEY_INTERFACE_SHOW_HIDDEN_FILES "Interface/View/ShowHiddenFiles"

/////////////////////////////////////////////////
// Options accessible via Settings interface
/////////////////////////////////////////////////

// Interface
#define KEY_INTERFACE_RESPECT_LAST_CURSOR_POS "Interface/Selection/RespectLastCursorPosition"
#define KEY_INTERFACE_NUMBERS_AFFTER_LETTERS "Interface/Sorting/NumbersAfterLetters"
#define KEY_INTERFACE_FILE_LIST_FONT "Interface/View/FileListFont"
#define INTERFACE_FILE_LIST_FONT_DEFAULT "Roboto Mono,9,-1,5,25,0,0,0,0,0,Light"
#define KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS "Interface/View/ShowSpecialFolderIcons"

// Operations
#define KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION "Operations/CopyMove/AskForConfirmation"

// Editing
#define KEY_EDITOR_PATH "Interface/Sorting/NumbersAfterLetters"

// Other
#define KEY_OTHER_SHELL_COMMAND_NAME "Other/Shell/ShellCommandName"
#define KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY "Other/UpdateChecking/CheckAutomatically"
