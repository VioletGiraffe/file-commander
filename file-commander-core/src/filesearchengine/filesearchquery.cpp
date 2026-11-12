#include "filesearchquery.h"

#include "cfilesystemobject.h"
#include "cpanel.h"
#include "filelist/filenamefilter.h"
#include "filesystemhelperfunctions.h"

#include <utility>
#include <vector>

static constexpr QLatin1StringView searchInFieldSeparator{ "; " };

QStringList nameFiltersFromSearchField(const QString& fieldText, const bool partialMatch)
{
	QStringList filters = splitNameFilterField(fieldText);
	// The filter language matches an unanchored filter anywhere in the name
	if (partialMatch)
		return filters;

	for (QString& filter : filters)
	{
		if (filter != u'*') // Already matches any name, and anchoring it would cost the engine its no-filter shortcut
			filter.prepend(u'^').append(u'$');
	}

	return filters;
}

QStringList rootsFromSearchInField(const QString& fieldText, const QString& baseDir)
{
	QStringList roots;
	for (const QString& pathText : fieldText.split(searchInFieldSeparator))
	{
		if (QString root = resolveUserPath(pathText, baseDir); !root.isEmpty())
			roots.push_back(std::move(root));
	}

	return roots;
}

QString searchInFieldTextFor(const CPanel& panel)
{
	const std::vector<CFileSystemObject> selectedItems = panel.selectedItems();
	if (selectedItems.empty())
		return panel.currentDirPathNative();

	QStringList paths;
	for (const CFileSystemObject& item : selectedItems)
		paths.push_back(toNativeSeparators(item.fullAbsolutePath()));

	return paths.join(searchInFieldSeparator);
}

bool searchInFieldTextBelongsInHistory(const QString& fieldText)
{
	return !fieldText.contains(searchInFieldSeparator);
}
