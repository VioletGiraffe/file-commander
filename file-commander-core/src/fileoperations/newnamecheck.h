#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// Why a name a user proposed will not be created. The caller supplies the wording, so UI text stays in the UI.
enum class NameRejection
{
	None,
	Empty,
	WhitespaceOnly,
	Separator,         // Would span more than one directory level
	DotComponent,      // "." and ".." are path syntax, never names
	InvalidCharacter,  // Embedded NUL everywhere; additional native-forbidden characters on Windows
	ReservedDeviceName, // Windows: CON, NUL, COM1, etc., with or without an extension
	TrailingDotOrSpace // Windows: NTFS stores it, Win32 strips it, so nothing else can reach the entry afterwards
};

// The one check for a name a user proposes, whether renaming or creating. The text is judged exactly as entered -
// never trimmed or otherwise repaired - so an accepted name is the name that gets created.
[[nodiscard]] NameRejection checkNewEntryName(const QString& name);

// The same rules per component, for the commands that take a relative path ("a", "a/b/c") and create the whole
// chain. Reports the first component that fails.
[[nodiscard]] NameRejection checkNewEntryPath(const QString& relativePath);
