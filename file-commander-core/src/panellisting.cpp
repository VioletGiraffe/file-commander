#include "panellisting.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "directoryscan/directoryscanner.h"
#include "directoryscan/thinioqstring.h"
#include "paths/pathsyntax.h"


#include <algorithm>
#include <iterator>
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

bool isFlattenedListingItem(const CFileSystemObject& item, const bool showHiddenFiles)
{
	return item.isFile() && item.exists() && (showHiddenFiles || !item.isHidden());
}

bool addFlattenedListingItems(const QString& folderPath, const bool showHiddenFiles, FileListHashMap& items, const std::atomic<bool>& abort, std::atomic<size_t>* filesFound)
{
	bool holdsFolderLink = false;
	scanDirectory(folderPath, [&items, showHiddenFiles, filesFound, &holdsFolderLink](const QString& parentPath, const thin_io::directory_entry& entry, bool /*reachedThroughLink*/) {
		CFileSystemObject item{ parentPath, entry };
		holdsFolderLink |= item.isLink() && item.isDir();
		if (!isFlattenedListingItem(item, showHiddenFiles))
			return;

		items.try_emplace(item.hash(), std::move(item));
		if (filesFound)
			filesFound->store(items.size(), std::memory_order_relaxed);
	}, abort);
	return holdsFolderLink;
}

FlattenedListingRecheck recheckFlattenedListing(const std::vector<CFileSystemWatcher::ChangedPath>& changes, const bool showHiddenFiles, const std::atomic<bool>& abort)
{
	FlattenedListingRecheck recheck;
	for (const CFileSystemWatcher::ChangedPath& change : changes)
	{
		if (abort)
			break;

		if (change.contentsMayBeGone)
			recheck.vanishedFolderCandidates.push_back(withTrailingSeparator(change.path));

		const auto entry = getDirectoryEntry(change.path);
		CFileSystemObject item = entry ? CFileSystemObject{ parentFolderOf(change.path).toString(), *entry } : CFileSystemObject{};
		// A folder link, or a folder holding one
		if (change.contentsMayBeNew && item.isDir() && (item.isLink() || addFlattenedListingItems(change.path, showHiddenFiles, recheck.upserted, abort)))
		{
			recheck.listAgain = true;
			return recheck;
		}

		const bool listed = isFlattenedListingItem(item, showHiddenFiles);
		// Windows reads back the name's spelling on disk: after a rename that changed only its case, the old spelling is gone
		if (!listed || item.fullAbsolutePath() != change.path)
			recheck.removedHashes.push_back(pathHash(change.path));
		if (listed)
			recheck.upserted.try_emplace(item.hash(), std::move(item));
	}

	// A path read as gone can turn up in a folder scanned after it: the later read wins
	std::erase_if(recheck.removedHashes, [&recheck](qulonglong hash) { return recheck.upserted.contains(hash); });
	return recheck;
}

std::vector<qulonglong> removeItemsUnderFolders(FileListHashMap& items, std::vector<QString> folders, const FileListHashMap& keep)
{
	std::vector<qulonglong> removedHashes;
	if (folders.empty())
		return removedHashes;

	// The paths under a folder sort right after it, so dropping the nested folders leaves each item at most one candidate:
	// the last folder that sorts before its path
	std::ranges::sort(folders);
	std::vector<QString> outermostFolders;
	for (QString& folder : folders)
	{
		if (outermostFolders.empty() || !folder.startsWith(outermostFolders.back()))
			outermostFolders.push_back(std::move(folder));
	}

	std::erase_if(items, [&](const auto& hashAndItem) {
		const QString& path = hashAndItem.second.fullAbsolutePath();
		const auto next = std::ranges::upper_bound(outermostFolders, path);
		if (next == outermostFolders.begin() || !path.startsWith(*std::prev(next)) || keep.contains(hashAndItem.first))
			return false;

		removedHashes.push_back(hashAndItem.first);
		return true;
	});
	return removedHashes;
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
