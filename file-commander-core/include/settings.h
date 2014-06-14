#pragma once

/////////////////////////////////////////////////
// Internal values persisted between sessions
/////////////////////////////////////////////////

#define KEY_LPANEL_PATH "Internal/Core/LPanel/Path"
#define KEY_RPANEL_PATH "Internal/Core/RPanel/Path"
#define KEY_LAST_PATH_FOR_DRIVE_L QString("Internal/Core/LPanel/LastPathForDrive%1")
#define KEY_LAST_PATH_FOR_DRIVE_R QString("Internal/Core/RPanel/LastPathForDrive%1")

#define KEY_LAST_COMMANDS_EXECUTED "Internal/Interface/LastCommandsExecuted"

#define KEY_HISTORY_L "Internal/Core/LPanel/History"
#define KEY_HISTORY_R "Internal/Core/RPanel/History"

#define KEY_FAVORITES "Internal/Core/Favorites"



/////////////////////////////////////////////////
// Options accessible via Settings interface
/////////////////////////////////////////////////

// Interface
#define KEY_INTERFACE_NUMBERS_AFFTER_LETTERS "Interface/Sorting/NumbersAfterLetters"
#define KEY_INTERFACE_SHOW_HIDDEN_FILES      "Interface/View/ShowHiddenFiles"

// Operations
#define KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION "Operations/CopyMove/AskForConfirmation"

// Editing
#define KEY_EDITOR_PATH "Interface/Sorting/NumbersAfterLetters"

// Other
#define KEY_OTHER_SHELL_COMMAND_NAME "Other/Shell/ShellCommandName"
