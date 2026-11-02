#include "panellisting.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "directoryscan/thinioqstring.h"


#include <algorithm>
#include <utility>

std::optional<FileListHashMap> listDirectoryForPanel(const QString& dirPath, const bool showHiddenFiles)
{
	const auto entries = listDirectoryWithDetails(dirPath);
	if (!entries)
		return std::nullopt;

	FileListHashMap items;
	items.reserve(entries->size() + 1); // + 1: [..]
	for (const auto& entry : *entries)
	{
		CFileSystemObject object{ dirPath, entry };
		if ((!object.isFile() && !object.isDir()) || (!showHiddenFiles && object.isHidden()))
			continue; // Could be a socket

		const qulonglong hash = object.hash();
		items.try_emplace(hash, std::move(object));
	}

	if (auto cdUp = CFileSystemObject::cdUpEntryOf(dirPath))
	{
		const qulonglong hash = cdUp->hash();
		items.try_emplace(hash, std::move(*cdUp));
	}

	return items;
}

static QString withTrailingSeparator(const QString& path)
{
	return path.endsWith('/') ? path : path + '/';
}

ListableFolderProbe::ListableFolderProbe(const std::atomic<bool>& cancelled, std::function<bool(const QString&)> canList) :
	_cancelled{ cancelled },
	_canList{ std::move(canList) }
{
}

bool ListableFolderProbe::isListable(const QString& folder)
{
	if (_cancelled || isUnderUnlistableFolder(folder))
		return false;

	if (_canList(folder))
		return true;

	addUnlistable(folder);
	return false;
}

void ListableFolderProbe::addUnlistable(const QString& folder)
{
	if (isUnderUnlistableFolder(folder))
		return;

	// The folder to skip from now on is the topmost unlistable one on the way down from the root.
	// Checked from the root down, so only that folder can wait out a timeout: the folders above it answer at once.
	const std::vector<QString> hierarchy = pathHierarchy(folder); // The folder first, the root last
	if (hierarchy.empty())
		return;

	QString topmostUnlistable = withTrailingSeparator(folder);
	for (size_t i = hierarchy.size() - 1; i > 0 && !_cancelled; --i)
	{
		if (!_canList(hierarchy[i]))
		{
			topmostUnlistable = withTrailingSeparator(hierarchy[i]);
			break;
		}
	}

	_unlistableFolders.push_back(std::move(topmostUnlistable));
}

bool ListableFolderProbe::isUnderUnlistableFolder(const QString& folder) const
{
	const QString folderWithSeparator = withTrailingSeparator(folder);
	return std::ranges::any_of(_unlistableFolders, [&folderWithSeparator](const QString& unlistable) { return folderWithSeparator.startsWith(unlistable); });
}
