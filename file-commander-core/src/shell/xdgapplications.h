#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <vector>

// The applications freedesktop.org desktop entries and MIME associations register for file types, and their launch.
// Used on Linux and FreeBSD; compiled everywhere, so its tests run on every platform.
namespace XdgApplications {

// Where the lookup reads, and the user's preferences it follows
struct Environment
{
	QString configHome;
	QStringList configDirs; // Most important first, as are dataDirs and desktops
	QString dataHome;
	QStringList dataDirs;
	QStringList desktops; // XDG_CURRENT_DESKTOP's names, lowercase
	QString localeName; // As QLocale::name() gives it, e.g. "de_DE"
};

// From the XDG Base Directory variables with the spec's defaults, XDG_CURRENT_DESKTOP and the system locale
[[nodiscard]] Environment currentEnvironment();

struct Application
{
	QString id; // The desktop file ID, e.g. "org.kde.kate.desktop"
	QString desktopFilePath;
	QString name; // In the environment's language where the entry has it
	QString icon; // A theme icon name or an absolute path; may be empty
	QString exec; // The Exec key, unescaped as a string value: the command line's quoting is still in it
	QString workingDir; // The Path key; may be empty
	bool terminal = false;
};

// The applications that open files of every one of `fileTypes`, the first file's default application first when it opens them all.
// Each entry holds the names one file's type is known by, the most specific first: its MIME type and aliases, then its ancestors'.
[[nodiscard]] std::vector<Application> applicationsForFileTypes(const std::vector<QStringList>& fileTypes, const Environment& environment);
// applicationsForFileTypes for the files' types as QMimeDatabase finds them: by name and contents for the first five, by name alone past those.
// A file past the fifth whose name does not give its type does not narrow the list.
[[nodiscard]] std::vector<Application> applicationsToOpen(const QStringList& paths, const Environment& environment);

// The command lines, program first, that open `files` in `application`: one, or one per file when its Exec takes a single file.
// Without a file field code in Exec, the files are not passed.
// The error is the reason the Exec value cannot be used, never empty.
[[nodiscard]] std::expected<std::vector<QStringList>, QString> commandLines(const Application& application, const QStringList& files);

#ifndef _WIN32
// Runs commandLines: in a terminal when the entry asks for one, in the entry's working folder or else the first file's folder.
// The error is the reason for the failure, never empty.
[[nodiscard]] std::expected<void, QString> openInApplication(const Application& application, const QStringList& files);
#endif

// For writing a desktop entry: `value` escaped as a string value
[[nodiscard]] QString escapedStringValue(QString value);
// For writing a desktop entry: `argument` quoted as one Exec argument, then escaped as a string value
[[nodiscard]] QString escapedExecArgument(const QString& argument);

}
