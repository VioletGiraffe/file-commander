#pragma once

#include "favoritelocationslist/cfavoritelocations.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

class QTreeWidgetItem;

namespace Ui {
class CFavoriteLocationsEditor;
}

class CFavoriteLocationsListItem;

class CFavoriteLocationsEditor final : public QDialog
{
public:
	explicit CFavoriteLocationsEditor(QWidget *parent = nullptr);
	~CFavoriteLocationsEditor() override;

private:
	void currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * previous);
	void contextMenu(const QPoint& pos);
	void nameEdited(const QString& newName);
	void locationEdited(const QString& newLocation);

	void fillUI();
	void addLocationsToTreeWidget(std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator & locationCollectionListIterator, QTreeWidgetItem * parent);

private:
	Ui::CFavoriteLocationsEditor *ui;
	CFavoriteLocations& _locations;
	CFavoriteLocationsListItem* _currentItem;
};
