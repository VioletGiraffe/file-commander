#include "directoryscanner.h"

#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <vector>

static void scanDirectoryRecursive(const CFileSystemObject& root,
	const std::function<void(const CFileSystemObject&, bool)>& observer,
	const std::atomic<bool>& abort,
	const bool followDirLinks,
	const bool reachedThroughLink,
	std::vector<QString>& linkTargetsBeingTraversed)
{
	if (abort)
		return;

	if (observer)
		observer(root, reachedThroughLink);

	if (!root.isDir())
		return;

	bool traversingLink = false;
	if (root.isLink())
	{
		if (!followDirLinks)
			return;

		// Cycle guards; only links pay the canonicalFilePath() / canonicalPath() calls, plain directory trees don't reach this code.
		const QString canonicalTarget = root.qFileInfo().canonicalFilePath();
		if (canonicalTarget.isEmpty()) // Broken link
			return;

		// A link pointing at its own ancestor is a cycle regardless of how this scan reached it
		const QString canonicalParentDir = root.qFileInfo().canonicalPath();
		if (canonicalParentDir == canonicalTarget || canonicalParentDir.startsWith(canonicalTarget % '/'))
			return;

		// A target already being traversed higher up this branch means the links form a loop
		if (std::find(linkTargetsBeingTraversed.begin(), linkTargetsBeingTraversed.end(), canonicalTarget) != linkTargetsBeingTraversed.end())
			return;

		linkTargetsBeingTraversed.push_back(canonicalTarget);
		traversingLink = true;
	}

	const auto list = QDir{root.fullAbsolutePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);
	for (const auto& entry : list)
	{
		if (abort)
			break;

		scanDirectoryRecursive(CFileSystemObject(entry), observer, abort, followDirLinks, reachedThroughLink || traversingLink, linkTargetsBeingTraversed);
	}

	if (traversingLink)
		linkTargetsBeingTraversed.pop_back();
}

void scanDirectory(const CFileSystemObject& root,
	const std::function<void(const CFileSystemObject&, bool)>& observer,
	const std::atomic<bool>& abort,
	const bool followDirLinks)
{
	std::vector<QString> linkTargetsBeingTraversed;
	scanDirectoryRecursive(root, observer, abort, followDirLinks, false, linkTargetsBeingTraversed);
}
