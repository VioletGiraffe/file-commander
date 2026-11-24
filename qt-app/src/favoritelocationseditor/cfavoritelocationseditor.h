#pragma once

#include "favoritelocationslist/cfavoritelocations.h"
#include "filesystemhelpers/folderavailabilitychecker.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
#include <QHash>
#include <QString>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <optional>

class QTreeWidgetItem;

namespace Ui {
class CFavoriteLocationsEditor;
}

// Edits a copy of the favorites held in the tree items: OK saves the tree, Cancel discards it
class CFavoriteLocationsEditor final : public QDialog
{
public:
	// currentFolderPosix: the folder a new location starts with, and the one relative paths are resolved against
	CFavoriteLocationsEditor(QWidget* parent, QString currentFolderPosix);
	~CFavoriteLocationsEditor() override;

	void accept() override;

private:
	[[nodiscard]] QTreeWidgetItem* newTreeItem(const CLocationsCollection& favorite);
	// After the current item, in its group; at the end of the top level without a current item
	void insertNewItem(const CLocationsCollection& favorite);
	void deleteSelectedItems();
	void browseForFolder(QTreeWidgetItem* item);

	// key: an arrow. Up and Down move within the group, Left out of it, and Right into the group above.
	void moveCurrentItem(Qt::Key key);
	void applyFilter(const QString& text);

	// Dims a location until its folder is found, and starts the check when its result is not known yet
	void updateAvailability(QTreeWidgetItem* item);
	void showAvailability(QTreeWidgetItem* item, std::optional<bool> available);
	void showFinishedAvailabilityChecks();

private:
	Ui::CFavoriteLocationsEditor* const ui;
	CFavoriteLocations& _favorites;
	const QString _currentFolderPosix;

	CFolderAvailabilityChecker _availabilityChecker;
	QHash<QString, bool> _folderAvailability; // By the path resolved with resolveUserPath()
	QTimer _availabilityPollTimer;
};
