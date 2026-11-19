#include "desktopentry.h"

#include "shell/xdgapplications.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

namespace {

QString appImagePath()
{
	return qEnvironmentVariable("APPIMAGE");
}

QString entryPath()
{
	return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) % '/' % DesktopEntry::id % ".desktop";
}

QString iconPath()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) % "/icons/hicolor/256x256/apps/" % DesktopEntry::id % ".png";
}

// appimagetool requires the desktop file and its icon at the AppDir root
QString appDirFilePath(const QLatin1StringView extension)
{
	return qEnvironmentVariable("APPDIR") % '/' % DesktopEntry::id % extension;
}

QString writeError(const QString& path, const QString& reason)
{
	return QCoreApplication::translate("DesktopEntry", "Cannot write %1: %2").arg(path, reason);
}

QString tryExecLine()
{
	return "TryExec=" % XdgApplications::escapedStringValue(appImagePath());
}

// The AppImage's own entry, launching this AppImage file
std::expected<QString, QString> entryText()
{
	QFile templateFile{ appDirFilePath(QLatin1StringView{ ".desktop" }) };
	if (!templateFile.open(QFile::ReadOnly | QFile::Text))
		return std::unexpected(QCoreApplication::translate("DesktopEntry", "Cannot read %1: %2").arg(templateFile.fileName(), templateFile.errorString()));

	QStringList lines = QString::fromUtf8(templateFile.readAll()).split('\n');
	for (QString& line : lines)
	{
		// TryExec hides the entry from menus once this AppImage file is moved or deleted
		if (line.startsWith(QLatin1StringView{ "Exec=" }))
			line = "Exec=" % XdgApplications::escapedExecArgument(appImagePath()) % '\n' % tryExecLine();
		else if (line.startsWith(QLatin1StringView{ "Icon=" }))
			line = "Icon=" % XdgApplications::escapedStringValue(iconPath());
	}

	return lines.join('\n');
}

std::expected<void, QString> createParentFolder(const QString& filePath)
{
	const QString folder = QFileInfo{ filePath }.absolutePath();
	if (!QDir{}.mkpath(folder))
		return std::unexpected(QCoreApplication::translate("DesktopEntry", "Cannot create the folder %1").arg(folder));
	return {};
}

std::expected<void, QString> removeFile(const QString& path)
{
	QFile file{ path };
	if (file.exists() && !file.remove())
		return std::unexpected(QCoreApplication::translate("DesktopEntry", "Cannot delete %1: %2").arg(path, file.errorString()));
	return {};
}

}

bool DesktopEntry::runningFromAppImage()
{
	// The executable is checked too: the AppImage runtime's environment reaches every program started from the AppImage
	const QString appDir = QFileInfo{ qEnvironmentVariable("APPDIR") }.canonicalFilePath();
	return !appDir.isEmpty() && !appImagePath().isEmpty() && QCoreApplication::applicationFilePath().startsWith(appDir + '/');
}

bool DesktopEntry::installedForThisAppImage()
{
	QFile entry{ entryPath() };
	return entry.open(QFile::ReadOnly | QFile::Text) && QString::fromUtf8(entry.readAll()).split('\n').contains(tryExecLine());
}

std::expected<void, QString> DesktopEntry::install()
{
	assert_r(runningFromAppImage());

	const auto text = entryText();
	if (!text)
		return std::unexpected(text.error());

	const QString icon = iconPath();
	if (const auto created = createParentFolder(icon); !created)
		return created;
	if (const auto removed = removeFile(icon); !removed)
		return removed;

	QFile sourceIcon{ appDirFilePath(QLatin1StringView{ ".png" }) };
	if (!sourceIcon.copy(icon))
		return std::unexpected(writeError(icon, sourceIcon.errorString()));

	// Atomic: the desktop watches this folder and may read the file at any time
	QSaveFile entry{ entryPath() };
	if (const auto created = createParentFolder(entry.fileName()); !created)
		return created;
	if (!entry.open(QFile::WriteOnly) || entry.write(text->toUtf8()) < 0 || !entry.commit())
		return std::unexpected(writeError(entry.fileName(), entry.errorString()));

	return {};
}

std::expected<void, QString> DesktopEntry::uninstall()
{
	// The entry goes first: a menu never shows it without its icon
	if (const auto removed = removeFile(entryPath()); !removed)
		return removed;
	return removeFile(iconPath());
}
