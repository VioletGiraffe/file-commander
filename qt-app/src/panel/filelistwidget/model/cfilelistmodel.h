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

	QVariant data (const QModelIndex & index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role);
	Qt::ItemFlags flags(const QModelIndex & index) const override;

signals:
	void itemEdited(qulonglong itemHash, QString newName);

private:
	qulonglong itemHash(const QModelIndex& index) const;

private:
	CController & _controller;
	QTreeView   * _tree;
	Panel         _panel;
};

#endif // CFILELISTMODEL_H
