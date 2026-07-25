#pragma once

#include "utils/naturalsorting/cnaturalsorterqcollator.h"

#include <QTreeWidget>

// Orders a column by the unsigned number stashed in its Qt::UserRole where there is one, so that sizes and timestamps
// sort by value instead of by their formatted text, and by natural text order everywhere else.
class CSortByDataTreeItem : public QTreeWidgetItem
{
public:
	using QTreeWidgetItem::QTreeWidgetItem;

	bool operator<(const QTreeWidgetItem& other) const override
	{
		const int column = treeWidget()->sortColumn();
		const QVariant sortKey = data(column, Qt::UserRole), otherSortKey = other.data(column, Qt::UserRole);
		if (sortKey.typeId() == QMetaType::ULongLong && otherSortKey.typeId() == QMetaType::ULongLong)
			return sortKey.toULongLong() < otherSortKey.toULongLong();

		return NaturalSort::lessThan(text(column), other.text(column));
	}
};
