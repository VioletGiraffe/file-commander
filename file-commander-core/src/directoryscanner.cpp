#include "directoryscanner.h"

#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

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

	const auto list = QDir{root.fullAbsolutePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);
	for (const auto& entry : list)
	{
		if (abort)
			break;

		scanDirectoryRecursive(CFileSystemObject(entry), observer, abort, followDirLinks, reachedThroughLink || traversingLink, dirsBeingScanned);
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
	const QFileInfoList directoryEntries = QDir{dirPath}.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
	for (const QFileInfo& directoryEntry : directoryEntries)
	{
#ifndef _WIN32
		// The root's ".." entry is itself (/.. == /); skip it so the root listing has no self-referential parent row.
		// Only the filesystem root yields this exact path. (Windows roots don't produce it.)
		if (directoryEntry.absoluteFilePath() == QLatin1String("/.."))
			continue;
#endif

		CFileSystemObject object(directoryEntry);
		if ((!object.isFile() && !object.isDir()) || !object.exists() || (!showHiddenFiles && object.isHidden()))
			continue; // Could be a socket

		const qulonglong hash = object.hash();
		items[hash] = std::move(object);
	}

	return items;
}
