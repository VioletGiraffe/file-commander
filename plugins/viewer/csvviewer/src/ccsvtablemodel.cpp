#include "ccsvtablemodel.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <numeric>
#include <utility>

static bool isNumber(QStringView text) noexcept
{
	bool ok = false;
	(void)text.toDouble(&ok);
	return ok;
}

CCsvTableModel::CCsvTableModel(QObject* parent) :
	QAbstractTableModel(parent)
{
}

void CCsvTableModel::setTable(CsvTable table)
{
	beginResetModel();
	_table = std::move(table);
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
	if (_table.rowCount() < 2)
		return false;

	constexpr size_t sampleRows = 100;
	const size_t sampleEnd = std::min(_table.rowCount(), 1 + sampleRows);
	const auto columnIsNumeric = [&](size_t column) {
		bool hasNumbers = false;
		for (size_t row = 1; row < sampleEnd; ++row)
		{
			const QStringView text = _table.cell(row, column);
			if (text.isEmpty())
				continue;
			if (!isNumber(text))
				return false;
			hasNumbers = true;
		}
		return hasNumbers;
	};

	bool hasNumericColumn = false;
	for (size_t column = 0; column < _table.columnCount; ++column)
	{
		const QStringView text = _table.cell(0, column);
		if (text.isEmpty() || isNumber(text))
			return false;

		hasNumericColumn = hasNumericColumn || columnIsNumeric(column);
	}

	return hasNumericColumn;
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
		if (isNumber(cellText(index.row(), index.column())))
			return (Qt::AlignRight | Qt::AlignVCenter).toInt();
		return {};
	default:
		return {};
	}
}

QVariant CCsvTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return {};

	if (orientation == Qt::Horizontal)
		return _firstRowIsHeader ? QVariant{ _table.cell(0, static_cast<size_t>(section)).toString() } : QVariant{ section + 1 };

	// The row's position in the file, 1-based like a spreadsheet: stays meaningful after sorting
	return static_cast<qulonglong>(_tableRowByModelRow[static_cast<size_t>(section)] + 1);
}

void CCsvTableModel::sort(int column, Qt::SortOrder order)
{
	assert_and_return_r(column >= -1 && column < columnCount(), );

	emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);

	const QModelIndexList persistentIndexes = persistentIndexList();
	std::vector<size_t> previousOrder;
	if (!persistentIndexes.isEmpty())
		previousOrder = _tableRowByModelRow;

	if (column < 0)
		resetRowOrder();
	else
	{
		const auto sortColumn = static_cast<size_t>(column);

		// Parsed once per row: the comparator runs n*log(n) times
		struct Key {
			double number;
			bool isNumber;
		};
		std::vector<Key> keyByTableRow(_table.rowCount());
		for (size_t row = 0; row < keyByTableRow.size(); ++row)
		{
			bool ok = false;
			const double number = _table.cell(row, sortColumn).toDouble(&ok);
			keyByTableRow[row] = { number, ok };
		}

		const auto lessThan = [&](size_t a, size_t b) {
			const Key& keyA = keyByTableRow[a];
			const Key& keyB = keyByTableRow[b];
			if (keyA.isNumber && keyB.isNumber)
				return keyA.number < keyB.number;
			if (keyA.isNumber != keyB.isNumber)
				return keyA.isNumber;
			return _table.cell(a, sortColumn).compare(_table.cell(b, sortColumn), Qt::CaseInsensitive) < 0;
		};

		if (order == Qt::AscendingOrder)
			std::stable_sort(_tableRowByModelRow.begin(), _tableRowByModelRow.end(), lessThan);
		else
			std::stable_sort(_tableRowByModelRow.begin(), _tableRowByModelRow.end(), [&lessThan](size_t a, size_t b) { return lessThan(b, a); });
	}

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

void CCsvTableModel::resetRowOrder()
{
	const size_t firstDataRow = _firstRowIsHeader && _table.rowCount() > 0 ? 1 : 0;
	_tableRowByModelRow.resize(_table.rowCount() - firstDataRow);
	std::iota(_tableRowByModelRow.begin(), _tableRowByModelRow.end(), firstDataRow);
}
