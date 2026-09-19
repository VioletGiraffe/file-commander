#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <vector>

// An entry of the Programs menu: a command line run the way the command line runs it
struct UserProgram
{
	enum class WorkingDir { CurrentPanel, OtherPanel, Custom };

	QString name;
	QString commandLine; // May contain placeholders, see expandPlaceholders
	WorkingDir workingDir = WorkingDir::CurrentPanel;
	QString customWorkingDir; // Only used with WorkingDir::Custom
	bool editBeforeRunning = false;

	bool operator==(const UserProgram&) const = default;
};

[[nodiscard]] std::vector<UserProgram> loadUserPrograms();
void saveUserPrograms(const std::vector<UserProgram>& programs);

enum class Placeholder { File, Name, Selection, Dir, Other, OtherFile };

[[nodiscard]] QString placeholderToken(Placeholder placeholder);

// The panel state placeholders expand to. Paths may use either separator.
struct PlaceholderValues
{
	QString currentDir;
	QString currentItem; // Empty when the cursor is on no item or on ".."
	std::vector<QString> selection; // The selected items, or the cursor item when nothing is selected
	QString otherDir;
	QString otherItem; // Empty when the cursor is on no item or on ".."
};

enum class PlaceholderError { NoCurrentItem, NoOtherItem };

// The folder `program` runs in, with native separators
[[nodiscard]] QString workingDirFor(const UserProgram& program, const PlaceholderValues& values);

// Replaces every placeholder with its value, quoted for the shell.
// Text in braces that is not a placeholder stays as typed.
[[nodiscard]] std::expected<QString, PlaceholderError> expandPlaceholders(const QString& commandLine, const PlaceholderValues& values);
