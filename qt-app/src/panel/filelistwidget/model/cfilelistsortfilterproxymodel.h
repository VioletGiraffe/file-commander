#pragma once

#include "cpanel.h"
#include "utils/naturalsorting/cnaturalsorting.h"
#include "../../../QtAppIncludes"

class CController;

class CFileListSortFilterProxyModel : public QSortFilterProxyModel
{
public:
	CFileListSortFilterProxyModel(QObject * parent);
	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(Panel p);

	void setSortingOptions(SortingOptions options);

protected:
	virtual bool lessThan(const QModelIndex &left, const QModelIndex &right) const;

private:
	CController   * _controller;
	Panel           _panel;
	CNaturalSorting _sorter;
};

