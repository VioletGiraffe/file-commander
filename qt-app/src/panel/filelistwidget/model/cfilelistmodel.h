#pragma once

#include "cpanel.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractItemModel>
RESTORE_COMPILER_WARNINGS

#include <vector>

enum Role {
	FullNameRole = Qt::UserRole+1
};

class CController;
class QTreeView;
class CFileListModel final : public QAbstractItemModel
{
	Q_OBJECT
public:
	explicit CFileListModel(Panel p, QObject *parent = nullptr);
	// Sets the position (left or right) of a panel that this model represents
	[[nodiscard]] Panel panelPosition() const;
	void onPanelContentsChanged(std::vector<qulonglong> itemHashes);

	[[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent) const override;
	[[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;

	[[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	[[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	[[nodiscard]] QVariant data(const QModelIndex & index, int role) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role) override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex & index) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

// Drag and drop
	[[nodiscard]] bool canDropMimeData(const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & parent) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList &indexes) const override;

	[[nodiscard]] qulonglong itemHash(int row) const;
	[[nodiscard]] qulonglong itemHash(const QModelIndex& index) const;

signals:
	void itemEdited(qulonglong itemHash, QString newName);

private:
	std::vector<qulonglong> _itemHashes;

	CController& _controller;
	const Panel _panel = Panel::UnknownPanel;
};
