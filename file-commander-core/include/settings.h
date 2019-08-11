#pragma once

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

constexpr const char* KEY_LPANEL_PATH = "Internal/Core/LPanel/Path";
constexpr const char* KEY_RPANEL_PATH = "Internal/Core/RPanel/Path";
constexpr const char* KEY_LAST_PATH_FOR_DRIVE_L = "Internal/Core/LPanel/LastPathForDrive%1";
constexpr const char* KEY_LAST_PATH_FOR_DRIVE_R = "Internal/Core/RPanel/LastPathForDrive%1";

constexpr const char* KEY_LAST_COMMANDS_EXECUTED = "Internal/Interface/LastCommandsExecuted";

constexpr const char* KEY_HISTORY_L = "Internal/Core/LPanel/History";
constexpr const char* KEY_HISTORY_R = "Internal/Core/RPanel/History";

constexpr const char* KEY_FAVORITES = "Internal/Core/Favorites";

// Copy/move/delete prompt dialog geometry
constexpr const char* KEY_PROMPT_DIALOG_GEOMETRY = "Internal/Interface/PropmptDialog/Geometry";

constexpr const char* KEY_LAST_UPDATE_CHECK_TIMESTAMP = "Internal/Interface/Update/LastUpdateCheckTimestamp";

/////////////////////////////////////////////////
// Options accessible via the main UI
/////////////////////////////////////////////////

constexpr const char* KEY_INTERFACE_SHOW_HIDDEN_FILES = "Interface/View/ShowHiddenFiles";

/////////////////////////////////////////////////
// Options accessible via Settings interface
/////////////////////////////////////////////////

// Interface
constexpr const char* KEY_INTERFACE_RESPECT_LAST_CURSOR_POS = "Interface/Selection/RespectLastCursorPosition";
constexpr const char* KEY_INTERFACE_NUMBERS_AFFTER_LETTERS = "Interface/Sorting/NumbersAfterLetters";
constexpr const char* KEY_INTERFACE_FILE_LIST_FONT = "Interface/View/FileListFont";
constexpr const char* INTERFACE_FILE_LIST_FONT_DEFAULT = "Roboto Mono,9,-1,5,25,0,0,0,0,0,Light";
constexpr const char* KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS = "Interface/View/ShowSpecialFolderIcons";
constexpr const char* KEY_INTERFACE_STYLE_SHEET = "Interface/Style/StylesheetText";

// Operations
constexpr const char* KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION = "Operations/CopyMove/AskForConfirmation";

// Editing
constexpr const char* KEY_EDITOR_PATH = "Edit/EditorProgramPath";

// Other
constexpr const char* KEY_OTHER_SHELL_COMMAND_NAME = "Other/Shell/ShellCommandName";
constexpr const char* KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY = "Other/UpdateChecking/CheckAutomatically";
