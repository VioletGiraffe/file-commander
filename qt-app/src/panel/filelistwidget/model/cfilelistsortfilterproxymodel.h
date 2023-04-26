#pragma once

#include "cpanel.h"
#include "utils/naturalsorting/cnaturalsorting.h"

DISABLE_COMPILER_WARNINGS
#include <QSortFilterProxyModel>
RESTORE_COMPILER_WARNINGS

class CController;

class CFileListSortFilterProxyModel final : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit CFileListSortFilterProxyModel(QObject * parent);
	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(Panel p);

	void setSortingOptions(SortingOptions options);

// Drag and drop
	bool canDropMimeData(const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & parent) const override;

	void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

signals:
	void sorted();

protected:
	[[nodiscard]] bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	CController   & _controller;
	Panel           _panel;
	CNaturalSorting _sorter;
};

