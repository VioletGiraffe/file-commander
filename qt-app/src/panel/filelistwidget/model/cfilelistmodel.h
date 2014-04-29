#ifndef CFILELISTMODEL_H
#define CFILELISTMODEL_H

#include "../../../QtAppIncludes"
#include "cpanel.h"

class CController;
class QTreeView;
class CFileListModel : public QStandardItemModel
{
	Q_OBJECT
public:
	explicit CFileListModel(QTreeView * treeview, QObject *parent = 0);
	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(Panel p);

	QTreeView * treeView() const;

private:
	CController & _controller;
	QTreeView   * _tree;
	Panel         _panel;
};

#endif // CFILELISTMODEL_H
