#include "directoryscanner.h"

#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h"


#include <utility>
#include <vector>

static void scanDirectoryRecursive(const CFileSystemObject& root,
	const std::function<void(const CFileSystemObject&, bool)>& observer,
	const std::atomic<bool>& abort,
	const bool followDirLinks,
	const bool reachedThroughLink,
	std::vector<QString>& dirsBeingScanned)
{
	if (abort)
		return;

	if (observer)
		observer(root, reachedThroughLink);

	if (!root.isDir())
		return;

	const bool traversingLink = root.isLink();
	if (traversingLink)
	{
		if (!followDirLinks)
			return;

		// Cycle guard: a link is only traversed if its target is not a directory already being scanned higher up this branch;
		// that covers links to their own ancestors as well as loops between links. Comparing filesystem identities rather than
		// paths sidesteps path normalization pitfalls (letter case, junction targets that Qt does not canonicalize).
		// Only links pay for the identity lookups; plain directory trees don't reach this code.
		const auto targetId = resolvedObjectId(root.fullAbsolutePath());
		if (!targetId) // Broken link, or a filesystem exposing no identity: refuse to follow rather than risk looping
			return;

		for (const QString& dirPath : dirsBeingScanned)
		{
			if (resolvedObjectId(dirPath) == targetId)
				return;
		}
	}

	dirsBeingScanned.push_back(root.fullAbsolutePath());

	if (const auto entries = listDirectoryWithDetails(root.fullAbsolutePath()))
	{
		for (const auto& entry : *entries)
		{
			if (abort)
				break;

			scanDirectoryRecursive(CFileSystemObject{ root.fullAbsolutePath(), entry }, observer, abort, followDirLinks, reachedThroughLink || traversingLink, dirsBeingScanned);
		}
	}

	dirsBeingScanned.pop_back();
}

void scanDirectory(const CFileSystemObject& root,
	const std::function<void(const CFileSystemObject&, bool)>& observer,
	const std::atomic<bool>& abort,
	const bool followDirLinks)
{
	std::vector<QString> dirsBeingScanned;
	scanDirectoryRecursive(root, observer, abort, followDirLinks, false, dirsBeingScanned);
}

FileListHashMap listDirectoryForPanel(const QString& dirPath, const bool showHiddenFiles)
{
	FileListHashMap items;
	const auto entries = listDirectoryWithDetails(dirPath);
	if (!entries)
		return items;

	for (const auto& entry : *entries)
	{
		CFileSystemObject object{ dirPath, entry };
		if ((!object.isFile() && !object.isDir()) || (!showHiddenFiles && object.isHidden()))
			continue; // Could be a socket

		const qulonglong hash = object.hash();
		items[hash] = std::move(object);
	}

	if (auto cdUp = CFileSystemObject::cdUpEntryOf(dirPath))
	{
		const qulonglong hash = cdUp->hash();
		items[hash] = std::move(*cdUp);
	}

	return items;
}
