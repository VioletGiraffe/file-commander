#include "filecontextmenu.h"

#include "shell/cshell.h"
#if !defined _WIN32 && !defined __APPLE__
#include "freedesktopshell.h"
#include "shell/xdgapplications.h"
#endif


// Submodule includes
#include "fileoperationsui/usermessagebox.h"
#include "shelloperations/cshelloperationrunner.h"
#include "widgets/widgetutils.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStringList>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
namespace {

struct OpenWithEntry
{
	QIcon icon;
	QString name;
	std::function<void()> open;
};

}

static UserMessage openFailure(const QString& applicationName, const QString& reason)
{
	return { MessageSeverity::Error, QCoreApplication::translate("FileContextMenu", "Failed to open with %1").arg(applicationName), reason };
}

// The applications that open every one of `paths`; a failure to open is shown over `parent`
static std::vector<OpenWithEntry> openWithEntries(const QStringList& paths, QWidget* parent)
{
	std::vector<OpenWithEntry> entries;
#ifdef __APPLE__
	const QFileIconProvider iconProvider;
	for (const OsShell::ApplicationBundle& application : OsShell::applicationsToOpen(paths))
	{
		entries.push_back({ .icon = iconProvider.icon(QFileInfo{ application.path }), .name = application.name, .open = [paths, application, parent = QPointer{ parent }] {
			// The launch reports a failure after the menu has closed: with the parent gone, the app is closing and nobody is told
			OsShell::openInApplication(paths, application.path, [name = application.name, parent](const QString& reason) {
				if (parent)
					showUserMessage(parent, openFailure(name, reason));
			});
		} });
	}
#else
	for (const XdgApplications::Application& application : XdgApplications::applicationsToOpen(paths, XdgApplications::currentEnvironment()))
	{
		const QString name = !application.name.isEmpty() ? application.name : application.id;
		entries.push_back({ .icon = QDir::isAbsolutePath(application.icon) ? QIcon{ application.icon } : QIcon::fromTheme(application.icon), .name = name,
			.open = [paths, application, name, parent] {
				if (const auto opened = XdgApplications::openInApplication(application, paths); !opened)
					showUserMessage(parent, openFailure(name, opened.error()));
			} });
	}
#endif
	return entries;
}
#endif

#if !defined _WIN32 && !defined __APPLE__
// Show in File Manager and Properties, when a file manager provides them.
// `selectItems`: the file manager selects `targets` in their folder; else it opens `targets`, which must be folders.
static void addFileManagerActions(QMenu& menu, const QStringList& targets, const bool selectItems, QWidget* parent)
{
	if (!FreedesktopShell::fileManagerAvailable())
		return;

	menu.addAction(QCoreApplication::translate("FileContextMenu", "Show in File Manager"), [targets, selectItems, parent] {
		if (selectItems)
			FreedesktopShell::showItemsInFileManager(targets, parent);
		else
			FreedesktopShell::showFoldersInFileManager(targets, parent);
	});
	menu.addAction(QCoreApplication::translate("FileContextMenu", "Properties"), [targets, parent] {
		FreedesktopShell::showPropertiesInFileManager(targets, parent);
	});
}

// Ejects on the runner's thread; a failure is shown over `parent`, unless it is gone by then
static void ejectVolume(const VolumeInfo& volume, QWidget* parent, CShellOperationRunner& shellOperations)
{
	shellOperations.run([devicePath = volume.devicePath, name = volume.volumeLabel, parent = QPointer{ parent }, &shellOperations] {
		const auto ejected = FreedesktopShell::ejectBlockDevice(devicePath);
		if (ejected)
			return;

		// The runner outlives its operations
		QMetaObject::invokeMethod(&shellOperations, [parent, name, reason = ejected.error()] {
			if (parent)
				showUserMessage(parent, { MessageSeverity::Error, QCoreApplication::translate("FileContextMenu", "Failed to eject %1").arg(name), reason });
		}, Qt::QueuedConnection);
	});
}
#endif

void FileContextMenu::showForItems(const std::vector<CFileSystemObject>& items, const QString& folder, QWidget* parent, [[maybe_unused]] const std::function<void(const QString& folderPath)>& enterFolder)
{
#ifdef _WIN32
	std::vector<std::wstring> paths;
	for (const CFileSystemObject& item : items)
		paths.push_back(item.fullAbsolutePath().toStdWString());

	if (paths.empty())
		paths.push_back(folder.toStdWString());

	OsShell::openShellContextMenuForObjects(paths, WidgetUtils::nativeOwnerWinId(parent));
#else
	QStringList itemPaths;
	for (const CFileSystemObject& item : items)
		itemPaths.push_back(item.fullAbsolutePath());

	const QStringList paths = !itemPaths.isEmpty() ? itemPaths : QStringList{ folder };

	QMenu menu{ parent };
	if (std::vector<OpenWithEntry> entries = openWithEntries(paths, parent); !entries.empty())
	{
		QMenu* openWith = menu.addMenu(QCoreApplication::translate("FileContextMenu", "Open With"));
		for (OpenWithEntry& entry : entries)
			openWith->addAction(entry.icon, entry.name, std::move(entry.open));
	}

	if (items.size() == 1 && items.front().isBundle())
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Show Package Contents"), [&] { enterFolder(items.front().fullAbsolutePath()); });

#ifndef __APPLE__
	addFileManagerActions(menu, paths, !itemPaths.isEmpty(), parent);
#endif

	if (!menu.isEmpty())
		menu.exec(QCursor::pos());
#endif
}

void FileContextMenu::showForVolume([[maybe_unused]] const VolumeInfo& volume, QWidget* parent, [[maybe_unused]] CShellOperationRunner& shellOperations)
{
#ifdef _WIN32
	OsShell::openShellContextMenuForObjects({ volume.rootObjectInfo.fullAbsolutePath().toStdWString() }, WidgetUtils::nativeOwnerWinId(parent));
#else
	QMenu menu{ parent };
#ifndef __APPLE__
	addFileManagerActions(menu, { volume.rootObjectInfo.fullAbsolutePath() }, false, parent);

	// An internal or network volume has no Eject
	const bool removable = volume.kind == VolumeInfo::Kind::External || volume.kind == VolumeInfo::Kind::Optical || volume.kind == VolumeInfo::Kind::DiskImage;
	if (removable && !volume.devicePath.isEmpty())
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Eject"), [&] { ejectVolume(volume, parent, shellOperations); });
#endif

	if (!menu.isEmpty())
		menu.exec(QCursor::pos());
#endif
}
