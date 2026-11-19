#include "filecontextmenu.h"

#include "shell/cshell.h"
#if !defined _WIN32 && !defined __APPLE__
#include "shell/xdgapplications.h"
#endif


// Submodule includes
#include "fileoperationsui/usermessagebox.h"
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
	QStringList paths;
	for (const CFileSystemObject& item : items)
		paths.push_back(item.fullAbsolutePath());

	if (paths.empty())
		paths.push_back(folder);

	QMenu menu{ parent };
	if (std::vector<OpenWithEntry> entries = openWithEntries(paths, parent); !entries.empty())
	{
		QMenu* openWith = menu.addMenu(QCoreApplication::translate("FileContextMenu", "Open With"));
		for (OpenWithEntry& entry : entries)
			openWith->addAction(entry.icon, entry.name, std::move(entry.open));
	}

	if (items.size() == 1 && items.front().isBundle())
		menu.addAction(QCoreApplication::translate("FileContextMenu", "Show Package Contents"), [&] { enterFolder(items.front().fullAbsolutePath()); });

	if (!menu.isEmpty())
		menu.exec(QCursor::pos());
#endif
}
