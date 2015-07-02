#ifndef CFAVORITELOCATIONSEDITOR_H
#define CFAVORITELOCATIONSEDITOR_H

#include "favoritelocationslist/cfavoritelocations.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

class QTreeWidgetItem;

namespace Ui {
class CFavoriteLocationsEditor;
}

class CFavoriteLocationsListItem;

class CFavoriteLocationsEditor : public QDialog
{
	Q_OBJECT

public:
	explicit CFavoriteLocationsEditor(QWidget *parent = 0);
	~CFavoriteLocationsEditor();

private slots:
	void currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * previous);
	void contextMenu(const QPoint& pos);
	void nameEdited(QString newName);
	void locationEdited(QString newLocation);

private:
	void fillUI();
	void addLocationsToTreeWidget(std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator & locationCollectionListIterator, QTreeWidgetItem * parent);

private:
	Ui::CFavoriteLocationsEditor *ui;
	CFavoriteLocations& _locations;
	CFavoriteLocationsListItem* _currentItem;
};

#endif // CFAVORITELOCATIONSEDITOR_H
