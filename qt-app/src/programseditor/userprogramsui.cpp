#include "userprogramsui.h"

#include "shell/cshell.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFileIconProvider>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

QKeySequence userProgramShortcut(const size_t position)
{
	static constexpr size_t functionKeyCount = 12;
	if (position >= functionKeyCount)
		return {};

	return QKeySequence{ Qt::CTRL | Qt::SHIFT | static_cast<Qt::Key>(Qt::Key_F1 + static_cast<int>(position)) };
}

QIcon userProgramIcon(const UserProgram& program)
{
	const QString workingDir = program.workingDir == UserProgram::WorkingDir::Custom ? program.customWorkingDir : QString{};
	const QString programPath = OsShell::commandLineProgramPath(program.commandLine, workingDir);
	return programPath.isEmpty() ? QFileIconProvider{}.icon(QFileIconProvider::File) : QFileIconProvider{}.icon(QFileInfo{ programPath });
}

QString placeholderErrorText(const PlaceholderError error)
{
	switch (error)
	{
	case PlaceholderError::NoCurrentItem:
		return QCoreApplication::translate("UserPrograms", "No file or folder is under the cursor.");
	case PlaceholderError::NoOtherItem:
		return QCoreApplication::translate("UserPrograms", "No file or folder is under the cursor in the other panel.");
	}

	return {};
}

QString commandLineTooLongText(const qsizetype length, const qsizetype maxLength)
{
	return QCoreApplication::translate("UserPrograms", "The command line is %1 characters long, the limit is %2. Select fewer items.").arg(length).arg(maxLength);
}
