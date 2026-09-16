#pragma once

#include "ccsvparser.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractTableModel>
RESTORE_COMPILER_WARNINGS

#include <vector>

// Read-only view of a CsvTable. Sorting permutes the row order; the table itself never changes.
class CCsvTableModel final : public QAbstractTableModel
{
public:
	explicit CCsvTableModel(QObject* parent = nullptr);

	void setTable(CsvTable table);
	// The first table row becomes the horizontal header instead of a data row; also drops the current sort
	void setFirstRowIsHeader(bool isHeader);
	// Row 0 has cells, and every one is non-empty and non-numeric
	[[nodiscard]] bool firstRowLooksLikeHeader() const;
	[[nodiscard]] const CsvTable& table() const noexcept { return _table; }

	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	// Numbers ascend before text; column -1 restores file order
	void sort(int column, Qt::SortOrder order) override;

private:
	[[nodiscard]] QStringView cellText(int row, int column) const noexcept;
	void resetRowOrder();

private:
	CsvTable _table;
	std::vector<size_t> _tableRowByModelRow;
	bool _firstRowIsHeader = false;
};
