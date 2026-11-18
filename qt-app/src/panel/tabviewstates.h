#pragma once

#include "filelistwidget/cfilelistview.h"

#include "cpanel.h"

#include <optional>
#include <utility>
#include <vector>

// Each tab's column layout by tab id, one store per side
using TabColumnLayouts = std::vector<std::pair<qulonglong, CFileListView::ColumnLayout>>;

// Skips the records it cannot read
[[nodiscard]] TabColumnLayouts loadTabColumnLayouts(Panel side);
// Also removes the side's legacy header state
void saveTabColumnLayouts(Panel side, const TabColumnLayouts& layouts);

// The side's QHeaderView::saveState() blob, in settings older than the per-tab layouts
struct LegacySideHeaderState {
	CFileListView::ColumnLayout columnLayout;
	std::optional<std::pair<FileListSortField, Qt::SortOrder>> sorting;
};
// Defaults when the blob is absent or unreadable
[[nodiscard]] LegacySideHeaderState loadLegacySideHeaderState(Panel side);
