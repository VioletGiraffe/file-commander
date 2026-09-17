#pragma once

#include "ccsvtablemodel.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractListModel>
RESTORE_COMPILER_WARNINGS

#include <vector>

// The table's comment rows as a flat list, for navigating between the sections they delimit.
// Empty while the table is sorted: comment rows are gone from it.
class CCsvCommentListModel final : public QAbstractListModel
{
public:
	explicit CCsvCommentListModel(const CCsvTableModel& table, QObject* parent = nullptr);

	// Must be called whenever the table changes: nothing here follows it on its own
	void refresh();

	[[nodiscard]] int tableRow(int row) const noexcept;
	// The nearest comment row after (before) tableRow, or -1 where there is none
	[[nodiscard]] int commentRowAfter(int tableRow) const noexcept;
	[[nodiscard]] int commentRowBefore(int tableRow) const noexcept;

	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
	const CCsvTableModel& _table;
	std::vector<int> _commentTableRows; // Ascending
};
