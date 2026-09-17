#include "ccsvtablemodel.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

// Empty for NaN: it is unordered, so a sort comparator must never see it as a number
static std::optional<double> toNumber(QStringView text) noexcept
{
	bool ok = false;
	const double number = text.toDouble(&ok);
	if (!ok || std::isnan(number))
		return {};
	return number;
}

CCsvTableModel::CCsvTableModel(QObject* parent) :
	QAbstractTableModel(parent)
{
}

void CCsvTableModel::setTable(CsvTable table)
{
	beginResetModel();
	_table = std::move(table);

	_firstDataTableRow.reset();
	for (size_t row = 0; row < _table.rowCount(); ++row)
	{
		if (!_table.isCommentRow[row])
		{
			_firstDataTableRow = row;
			break;
		}
	}

	resetRowOrder();
	endResetModel();
}

void CCsvTableModel::setFirstRowIsHeader(bool isHeader)
{
	if (isHeader == _firstRowIsHeader)
		return;

	beginResetModel();
	_firstRowIsHeader = isHeader;
	resetRowOrder();
	endResetModel();
}

bool CCsvTableModel::firstRowLooksLikeHeader() const
{
	if (!_firstDataTableRow)
		return false;

	const size_t headerRow = *_firstDataTableRow;

	constexpr size_t maxSampleRows = 100;
	std::vector<size_t> sampleRows;
	for (size_t row = headerRow + 1; row < _table.rowCount() && sampleRows.size() < maxSampleRows; ++row)
	{
		if (!_table.isCommentRow[row])
			sampleRows.push_back(row);
	}

	const auto columnIsNumeric = [&](size_t column) {
		bool hasNumbers = false;
		for (size_t row : sampleRows)
		{
			const QStringView text = _table.cell(row, column);
			if (text.isEmpty())
				continue;
			if (!toNumber(text))
				return false;
			hasNumbers = true;
		}
		return hasNumbers;
	};

	bool hasNumericColumn = false;
	for (size_t column = 0; column < _table.columnCount; ++column)
	{
		const QStringView text = _table.cell(headerRow, column);
		if (text.isEmpty() || toNumber(text))
			return false;

		hasNumericColumn = hasNumericColumn || columnIsNumeric(column);
	}

	return hasNumericColumn;
}

bool CCsvTableModel::isCommentRow(int row) const noexcept
{
	return _table.isCommentRow[_tableRowByModelRow[static_cast<size_t>(row)]];
}

int CCsvTableModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(_tableRowByModelRow.size());
}

int CCsvTableModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(_table.columnCount);
}

QVariant CCsvTableModel::data(const QModelIndex& index, int role) const
{
	switch (role)
	{
	case Qt::DisplayRole:
		return cellText(index.row(), index.column()).toString();
	case Qt::ToolTipRole:
	{
		// Only where the cell is likely truncated; a tooltip repeating a short cell is noise
		const QStringView text = cellText(index.row(), index.column());
		if (text.size() > 60 || text.contains(u'\n'))
			return text.toString();
		return {};
	}
	case Qt::TextAlignmentRole:
		if (toNumber(cellText(index.row(), index.column())))
			return (Qt::AlignRight | Qt::AlignVCenter).toInt();
		return {};
	case CommentRowRole:
		return isCommentRow(index.row());
	default:
		return {};
	}
}

QVariant CCsvTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return {};

	if (orientation == Qt::Horizontal)
	{
		const std::optional<size_t> headerRow = headerTableRow();
		return headerRow ? QVariant{ _table.cell(*headerRow, static_cast<size_t>(section)).toString() } : QVariant{ section + 1 };
	}

	// The row's position in the file, 1-based like a spreadsheet: stays meaningful after sorting
	return static_cast<qulonglong>(_tableRowByModelRow[static_cast<size_t>(section)] + 1);
}

void CCsvTableModel::sort(int column, Qt::SortOrder order)
{
	assert_and_return_r(column >= -1 && column < columnCount(), );

	// Entering or leaving a sort hides or shows the comment rows: a row count change, which a layout change cannot carry
	if (_table.commentRowCount > 0 && (column >= 0) != _isSorted)
	{
		beginResetModel();
		arrangeRows(column, order);
		endResetModel();
		return;
	}

	emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);

	const QModelIndexList persistentIndexes = persistentIndexList();
	std::vector<size_t> previousOrder;
	if (!persistentIndexes.isEmpty())
		previousOrder = _tableRowByModelRow;

	arrangeRows(column, order);

	if (!persistentIndexes.isEmpty())
	{
		std::vector<int> modelRowByTableRow(_table.rowCount(), -1);
		for (size_t modelRow = 0; modelRow < _tableRowByModelRow.size(); ++modelRow)
			modelRowByTableRow[_tableRowByModelRow[modelRow]] = static_cast<int>(modelRow);

		QModelIndexList updatedIndexes;
		updatedIndexes.reserve(persistentIndexes.size());
		for (const QModelIndex& previous : persistentIndexes)
			updatedIndexes.push_back(index(modelRowByTableRow[previousOrder[static_cast<size_t>(previous.row())]], previous.column()));

		changePersistentIndexList(persistentIndexes, updatedIndexes);
	}

	emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

QStringView CCsvTableModel::cellText(int row, int column) const noexcept
{
	return _table.cell(_tableRowByModelRow[static_cast<size_t>(row)], static_cast<size_t>(column));
}

std::optional<size_t> CCsvTableModel::headerTableRow() const noexcept
{
	return _firstRowIsHeader ? _firstDataTableRow : std::nullopt;
}

void CCsvTableModel::arrangeRows(int column, Qt::SortOrder order)
{
	if (column < 0)
	{
		resetRowOrder();
		return;
	}

	if (!_isSorted)
		std::erase_if(_tableRowByModelRow, [this](size_t row) { return _table.isCommentRow[row]; });
	_isSorted = true;

	const auto sortColumn = static_cast<size_t>(column);

	// Parsed once per row: the comparator runs n*log(n) times
	std::vector<std::optional<double>> numberByTableRow(_table.rowCount());
	for (size_t row : _tableRowByModelRow)
		numberByTableRow[row] = toNumber(_table.cell(row, sortColumn));

	const auto lessThan = [&](size_t a, size_t b) {
		const std::optional<double>& numberA = numberByTableRow[a];
		const std::optional<double>& numberB = numberByTableRow[b];
		if (numberA && numberB)
			return *numberA < *numberB;
		if (numberA.has_value() != numberB.has_value())
			return numberA.has_value();
		return _table.cell(a, sortColumn).compare(_table.cell(b, sortColumn), Qt::CaseInsensitive) < 0;
	};

	// Empty cells sort last whichever way the rest goes, as in a spreadsheet: they are absent values, not small ones
	const auto compare = [&](size_t a, size_t b) {
		const bool emptyA = _table.cell(a, sortColumn).isEmpty(), emptyB = _table.cell(b, sortColumn).isEmpty();
		if (emptyA || emptyB)
			return !emptyA && emptyB;

		return order == Qt::AscendingOrder ? lessThan(a, b) : lessThan(b, a);
	};

	std::stable_sort(_tableRowByModelRow.begin(), _tableRowByModelRow.end(), compare);
}

void CCsvTableModel::resetRowOrder()
{
	_isSorted = false;

	const std::optional<size_t> headerRow = headerTableRow();
	_tableRowByModelRow.clear();
	_tableRowByModelRow.reserve(_table.rowCount());
	for (size_t row = 0; row < _table.rowCount(); ++row)
	{
		if (row != headerRow)
			_tableRowByModelRow.push_back(row);
	}
}
