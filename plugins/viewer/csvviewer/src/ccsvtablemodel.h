#pragma once

#include "ccsvparser.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractTableModel>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <vector>

// Read-only view of a CsvTable. Sorting permutes the row order and hides comment rows; the table itself never changes.
class CCsvTableModel final : public QAbstractTableModel
{
public:
	// bool: the row is a comment row
	static constexpr int CommentRowRole = Qt::UserRole;

	explicit CCsvTableModel(QObject* parent = nullptr);

	void setTable(CsvTable table);
	// The first non-comment row becomes the horizontal header instead of a data row; also drops the current sort
	void setFirstRowIsHeader(bool isHeader);
	// The first non-comment row's cells are all non-empty text, and in the next 100 data rows some column holds only numbers (empty cells aside).
	// An all-text table has no header: nothing tells a header from data.
	[[nodiscard]] bool firstRowLooksLikeHeader() const;
	[[nodiscard]] bool isCommentRow(int row) const noexcept;
	[[nodiscard]] const CsvTable& table() const noexcept { return _table; }

	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	// Numbers ascend before text; column -1 restores file order, comment rows included
	void sort(int column, Qt::SortOrder order) override;

private:
	[[nodiscard]] QStringView cellText(int row, int column) const noexcept;
	[[nodiscard]] std::optional<size_t> headerTableRow() const noexcept;
	void arrangeRows(int column, Qt::SortOrder order);
	void resetRowOrder();

private:
	CsvTable _table;
	std::vector<size_t> _tableRowByModelRow;
	std::optional<size_t> _firstDataTableRow; // Empty when every row is a comment
	bool _firstRowIsHeader = false;
	bool _isSorted = false;
};
