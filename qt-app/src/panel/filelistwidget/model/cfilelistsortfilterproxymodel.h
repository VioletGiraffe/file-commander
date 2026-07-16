#pragma once

#include "cpanel.h"

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

// Drag and drop
	bool canDropMimeData(const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & parent) const override;

	void sort(int column, Qt::SortOrder order) override;

	// The topmost row holding a file (folders always sort above files), or -1 if there are no files
	[[nodiscard]] int firstFileRow() const;

signals:
	void sorted();

protected:
	[[nodiscard]] bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	CController   & _controller;
	Panel           _panel = Panel::UnknownPanel;
};

