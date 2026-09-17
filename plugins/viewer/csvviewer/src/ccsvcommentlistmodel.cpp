#include "ccsvcommentlistmodel.h"


#include <algorithm>
#include <iterator>

CCsvCommentListModel::CCsvCommentListModel(const CCsvTableModel& table, QObject* parent) :
	QAbstractListModel(parent),
	_table(table)
{
}

void CCsvCommentListModel::refresh()
{
	beginResetModel();

	_commentTableRows.clear();
	for (int row = 0, rowCount = _table.rowCount(); row < rowCount; ++row)
	{
		if (_table.isCommentRow(row))
			_commentTableRows.push_back(row);
	}

	endResetModel();
}

int CCsvCommentListModel::tableRow(int row) const noexcept
{
	return _commentTableRows[static_cast<size_t>(row)];
}

int CCsvCommentListModel::commentRowAfter(int tableRow) const noexcept
{
	const auto next = std::upper_bound(_commentTableRows.begin(), _commentTableRows.end(), tableRow);
	return next != _commentTableRows.end() ? *next : -1;
}

int CCsvCommentListModel::commentRowBefore(int tableRow) const noexcept
{
	const auto next = std::lower_bound(_commentTableRows.begin(), _commentTableRows.end(), tableRow);
	return next != _commentTableRows.begin() ? *std::prev(next) : -1;
}

int CCsvCommentListModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(_commentTableRows.size());
}

QVariant CCsvCommentListModel::data(const QModelIndex& index, int role) const
{
	if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
		return {};

	return _table.index(tableRow(index.row()), 0).data(Qt::DisplayRole);
}
