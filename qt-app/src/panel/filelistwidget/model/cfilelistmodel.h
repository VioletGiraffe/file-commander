#pragma once

#include "cpanel.h"

DISABLE_COMPILER_WARNINGS
#include <QStandardItemModel>
RESTORE_COMPILER_WARNINGS

enum Role {
	FullNameRole = Qt::UserRole+1
};

class CController;
class QTreeView;
class CFileListModel : public QStandardItemModel
{
	Q_OBJECT
public:
	explicit CFileListModel(QTreeView * treeview, QObject *parent = nullptr);
	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(Panel p);
	[[nodiscard]] Panel panelPosition() const;

	[[nodiscard]] QTreeView * treeView() const;

	[[nodiscard]] QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role) override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex & index) const override;

// Drag and drop
	[[nodiscard]] bool canDropMimeData(const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & parent) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	[[nodiscard]] bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList &indexes) const override;

	[[nodiscard]] qulonglong itemHash(const QModelIndex& index) const;

signals:
	void itemEdited(qulonglong itemHash, QString newName);

private:
	CController & _controller;
	QTreeView   * _tree = nullptr;
	Panel         _panel = UnknownPanel;
};
