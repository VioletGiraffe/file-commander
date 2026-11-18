#include "favoritelocationsmenu.h"

#include "cfavoritelocationseditor.h"

#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "fileoperationsui/usermessagebox.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
RESTORE_COMPILER_WARNINGS

#include <utility>
#include <vector>

namespace {

struct MenuContext {
	CFavoriteLocations& favorites;
	QString currentFolderPosix;
	QWidget& dialogParent;
	std::function<void(const QString& path)> goTo;
};

} // namespace

// The actions run during the menu's exec(), after this returns: they copy what they use and never refer to the context
static void addLocationsToMenu(QMenu& menu, std::vector<CLocationsCollection>& locations, const MenuContext& context)
{
	for (CLocationsCollection& item : locations)
	{
		if (item.isCategory())
		{
			addLocationsToMenu(*menu.addMenu(item.displayName), item.subLocations, context);
			continue;
		}

		QAction* const action = menu.addAction(item.displayName);
		// Typed into the favorites editor: may be relative or hold variables
		const QString path = resolveUserPath(item.absolutePath, context.currentFolderPosix);
		action->setEnabled(!path.isEmpty());
		if (CFileSystemObject{ path } == CFileSystemObject{ context.currentFolderPosix })
		{
			action->setCheckable(true);
			action->setChecked(true);
		}

		assert_r(QObject::connect(action, &QAction::triggered, action, [goTo = context.goTo, path] { goTo(path); }));
	}

	if (!locations.empty())
		menu.addSeparator();

	QAction* const addFolderAction = menu.addAction(QObject::tr("Add current folder here..."));
	assert_r(QObject::connect(addFolderAction, &QAction::triggered, addFolderAction,
		[&favorites = context.favorites, &dialogParent = context.dialogParent, &list = locations, path = toNativeSeparators(context.currentFolderPosix)] {
		const QString displayName = CFileSystemObject(path).name();
		const QString name = QInputDialog::getText(&dialogParent, QObject::tr("Enter the name"), QObject::tr("Enter the name to store the current location under"), QLineEdit::Normal, displayName.isEmpty() ? path : displayName);
		if (name.isEmpty()) // Cancelled, or no name given
			return;

		if (const auto refusal = favorites.addLocation(list, name, path))
			showUserMessage(&dialogParent, *refusal);
	}));

	QAction* const addCategoryAction = menu.addAction(QObject::tr("Add a new subcategory..."));
	assert_r(QObject::connect(addCategoryAction, &QAction::triggered, addCategoryAction, [&favorites = context.favorites, &dialogParent = context.dialogParent, &list = locations] {
		const QString name = QInputDialog::getText(&dialogParent, QObject::tr("Enter the name"), QObject::tr("Enter the name for the new subcategory"));
		if (name.isEmpty()) // Cancelled, or no name given
			return;

		if (const auto refusal = favorites.addCategory(list, name))
			showUserMessage(&dialogParent, *refusal);
	}));
}

void fillFavoriteLocationsMenu(QMenu& menu, CFavoriteLocations& favorites, const QString& currentFolderPosix, QWidget& dialogParent, std::function<void(const QString& path)> goTo)
{
	addLocationsToMenu(menu, favorites.locations(), MenuContext{ favorites, currentFolderPosix, dialogParent, std::move(goTo) });

	menu.addSeparator();
	QAction* const edit = menu.addAction(QObject::tr("Edit..."));
	assert_r(QObject::connect(edit, &QAction::triggered, edit, [parent = &dialogParent] { CFavoriteLocationsEditor(parent).exec(); }));
}
