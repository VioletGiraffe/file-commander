#include "filecontextmenu.h"

#include "shell/cshell.h"
#ifdef __APPLE__
#include "macshell.h"
#elif !defined _WIN32
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
#include <QDesktopServices>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPoint>
#include <QPointer>
#include <QStringList>
#include <QUrl>
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

#ifndef _WIN32
// Show in Finder and Get Info on macOS; Show in File Manager and Properties elsewhere.
// `selectItems`: the file manager selects `targets`, all in one folder; else it opens `targets`, which must be folders.
static void addFileManagerActions(QMenu& menu, const QStringList& targets, const bool selectItems, QWidget* parent)
{
#ifdef __APPLE__
	menu.addAction(QCoreApplication::translate("FileContextMenu", "Show in Finder"), [targets, selectItems] {
		if (selectItems)
			MacShell::showItemsInFinder(targets);
		else
		{
			for (const QString& folder : targets)
				MacShell::showFolderInFinder(folder);
		}
	});
	menu.addAction(QCoreApplication::translate("FileContextMenu", "Get Info"), [targets, parent] { MacShell::showInfoInFinder(targets, parent); });
#else
	if (!FreedesktopShell::fileManagerAvailable())
	{
		// The folder opens in the desktop's default application for folders, with nothing selected. Properties has no such fallback.
		const QString folder = selectItems ? QFileInfo{ QDir::cleanPath(targets.front()) }.absolutePath() : targets.front();
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Show in File Manager"), [folder, parent] {
			if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) // QDesktopServices reports no reason
				showUserMessage(parent, { MessageSeverity::Error, QCoreApplication::translate("FileContextMenu", "Failed to show the folder in the file manager"), folder });
		});
		return;
	}

	menu.addAction(QCoreApplication::translate("FileContextMenu", "Show in File Manager"), [targets, selectItems, parent] {
		if (selectItems)
			FreedesktopShell::showItemsInFileManager(targets, parent);
		else
			FreedesktopShell::showFoldersInFileManager(targets, parent);
	});
	menu.addAction(QCoreApplication::translate("FileContextMenu", "Properties"), [targets, parent] {
		FreedesktopShell::showPropertiesInFileManager(targets, parent);
	});
#endif
}

static bool canEject(const VolumeInfo& volume)
{
	using Kind = VolumeInfo::Kind;
	const bool removable = volume.kind == Kind::External || volume.kind == Kind::Optical || volume.kind == Kind::DiskImage;
#ifdef __APPLE__
	return removable || volume.kind == Kind::Network; // Finder ejects a network volume too: it unmounts it
#else
	return removable && !volume.devicePath.isEmpty(); // UDisks manages block devices only
#endif
}

// Ejects on the runner's thread; a failure is shown over `parent`, unless it is gone by then
static void ejectVolume(const VolumeInfo& volume, QWidget* parent, CShellOperationRunner& shellOperations)
{
#ifdef __APPLE__
	auto eject = [rootPath = volume.rootObjectInfo.fullAbsolutePath()] { return MacShell::ejectVolume(rootPath); };
#else
	auto eject = [devicePath = volume.devicePath] { return FreedesktopShell::ejectBlockDevice(devicePath); };
#endif

	shellOperations.run([eject, name = volume.volumeLabel, parent = QPointer{ parent }, &shellOperations] {
		const auto ejected = eject();
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

	const QPoint menuPosition = QCursor::pos();
#ifdef __APPLE__
	// Quick Look and Share open their own window or menu: only once this menu has closed
	std::function<void()> afterMenu;
	if (!itemPaths.isEmpty())
	{
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Quick Look"), [&] {
			afterMenu = [&] { MacShell::showQuickLook(itemPaths, parent); };
		});
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Share..."), [&] {
			afterMenu = [&] { MacShell::showShareMenu(itemPaths, parent, menuPosition); };
		});
	}
#endif

	if (!menu.isEmpty())
		menu.addSeparator(); // Hidden when nothing follows it
	addFileManagerActions(menu, paths, !itemPaths.isEmpty(), parent);

	if (!menu.isEmpty())
		menu.exec(menuPosition);

#ifdef __APPLE__
	if (afterMenu)
		afterMenu();
#endif
#endif
}

void FileContextMenu::showForVolume(const VolumeInfo& volume, QWidget* parent, [[maybe_unused]] CShellOperationRunner& shellOperations)
{
#ifdef _WIN32
	OsShell::openShellContextMenuForObjects({ volume.rootObjectInfo.fullAbsolutePath().toStdWString() }, WidgetUtils::nativeOwnerWinId(parent));
#else
	QMenu menu{ parent };
	addFileManagerActions(menu, { volume.rootObjectInfo.fullAbsolutePath() }, false, parent);
	if (canEject(volume))
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Eject"), [&] { ejectVolume(volume, parent, shellOperations); });

	if (!menu.isEmpty())
		menu.exec(QCursor::pos());
#endif
}
