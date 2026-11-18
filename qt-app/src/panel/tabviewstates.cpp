#include "tabviewstates.h"

#include "columns.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QHeaderView>
#include <QSettings>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <cmath>
#include <type_traits>

#define KEY_LPANEL_TAB_VIEW_STATES QSL("Ui/LPanel/TabViewStates")
#define KEY_RPANEL_TAB_VIEW_STATES QSL("Ui/RPanel/TabViewStates")
#define KEY_LPANEL_LEGACY_HEADER_STATE QSL("Ui/LPanel/State")
#define KEY_RPANEL_LEGACY_HEADER_STATE QSL("Ui/RPanel/State")

static QString tabColumnLayoutsKey(const Panel side)
{
	return side == Panel::LeftPanel ? KEY_LPANEL_TAB_VIEW_STATES : KEY_RPANEL_TAB_VIEW_STATES;
}

static QString legacyHeaderStateKey(const Panel side)
{
	return side == Panel::LeftPanel ? KEY_LPANEL_LEGACY_HEADER_STATE : KEY_RPANEL_LEGACY_HEADER_STATE;
}

template <typename T>
static QString commaSeparated(const std::array<T, NumberOfColumns>& values)
{
	QStringList items;
	for (const T value : values)
	{
		if constexpr (std::is_floating_point_v<T>)
			items.push_back(QString::number(static_cast<double>(value)));
		else
			items.push_back(QString::number(value));
	}

	return items.join(',');
}

// Parses the two fields saveTabColumnLayouts() writes. Empty if they don't hold a valid layout.
static std::optional<CFileListView::ColumnLayout> parseColumnLayout(const QString& widthShares, const QString& visualOrder)
{
	const QStringList shareFields = widthShares.split(','), orderFields = visualOrder.split(',');
	if (shareFields.size() != NumberOfColumns || orderFields.size() != NumberOfColumns)
		return {};

	CFileListView::ColumnLayout layout;
	std::array<bool, NumberOfColumns> columnPlaced{};
	float totalShare = 0.0f;
	for (int i = 0; i < NumberOfColumns; ++i)
	{
		bool shareParsed = false, columnParsed = false;
		const float share = shareFields[i].toFloat(&shareParsed);
		const int column = orderFields[i].toInt(&columnParsed);
		if (!shareParsed || !std::isfinite(share) || share < 0.0f || !columnParsed || column < 0 || column >= NumberOfColumns || columnPlaced[(size_t)column])
			return {};

		columnPlaced[(size_t)column] = true;
		totalShare += share;
		layout.widthShares[(size_t)i] = share;
		layout.visualOrder[(size_t)i] = column;
	}

	if (totalShare <= 0.0f)
		return {};

	return layout;
}

TabColumnLayouts loadTabColumnLayouts(const Panel side)
{
	const QStringList records = QSettings{}.value(tabColumnLayoutsKey(side)).toStringList();

	TabColumnLayouts layouts;
	layouts.reserve((size_t)records.size());
	for (const QString& record : records)
	{
		// 3 fields: id, column widths, column order
		const QStringList fields = record.split('|');
		if (fields.size() != 3)
			continue; // Written by another version of this format

		bool idParsed = false;
		const qulonglong tabId = fields[0].toULongLong(&idParsed);
		if (!idParsed)
			continue;

		if (const auto columnLayout = parseColumnLayout(fields[1], fields[2]))
			layouts.emplace_back(tabId, *columnLayout);
	}

	return layouts;
}

void saveTabColumnLayouts(const Panel side, const TabColumnLayouts& layouts)
{
	QStringList records;
	records.reserve((qsizetype)layouts.size());
	for (const auto& [tabId, columnLayout] : layouts)
	{
		// Every field is numbers and commas, which can't contain the separator, so no escaping is needed.
		records.push_back(QString::number(tabId) + '|' + commaSeparated(columnLayout.widthShares) + '|' + commaSeparated(columnLayout.visualOrder));
	}

	QSettings s;
	s.setValue(tabColumnLayoutsKey(side), records);
	s.remove(legacyHeaderStateKey(side)); // Replaced by the store above
}

LegacySideHeaderState loadLegacySideHeaderState(const Panel side)
{
	LegacySideHeaderState state;
	// A detached header decodes the blob without disturbing the live one
	QHeaderView header{ Qt::Horizontal };
	if (!header.restoreState(QSettings{}.value(legacyHeaderStateKey(side)).toByteArray()))
		return state;

	if (const auto columnLayout = CFileListView::columnLayoutOf(header))
		state.columnLayout = *columnLayout;

	// The sort indicator is the only record of how the side was sorted
	const int sortColumn = header.sortIndicatorSection();
	if (sortColumn >= 0 && sortColumn < NumberOfColumns)
		state.sorting.emplace(sortFieldOf(sortColumn), header.sortIndicatorOrder());

	return state;
}
