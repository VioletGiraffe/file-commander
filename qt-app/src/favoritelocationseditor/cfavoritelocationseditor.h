#ifndef CFAVORITELOCATIONSEDITOR_H
#define CFAVORITELOCATIONSEDITOR_H

#include "QtAppIncludes"
#include "favoritelocationslist/cfavoritelocations.h"

namespace Ui {
class CFavoriteLocationsEditor;
}

class CFavoriteLocationsEditor : public QDialog
{
	Q_OBJECT

public:
	explicit CFavoriteLocationsEditor(QWidget *parent = 0);
	~CFavoriteLocationsEditor();

private slots:
	void currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * previous);
	void contextMenu(const QPoint& pos);

private:
	void fillUI();
	void addLocationsToTreeWidget(std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator & locationCollectionListIterator, QTreeWidgetItem * parent);

private:
	Ui::CFavoriteLocationsEditor *ui;
	CFavoriteLocations& _locations;
};

#endif // CFAVORITELOCATIONSEDITOR_H
