#include "foldercomparison.h"

#include "directoryscanner.h"
#include "filecontentcomparison.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <map>
#include <utility>

namespace {

struct SideEntry {
	QString relativePath;
	FileSystemObjectType type = UnknownType;
	uint64_t size = 0;
	bool isDirectory = false;
};

using SideMap = std::map<QString /* relative path */, SideEntry>;

QString rootPrefix(const QString& root)
{
	const QString prefix = CFileSystemObject{ root }.fullAbsolutePath();
	assert_debug_only(prefix.isEmpty() || prefix.endsWith('/'));
	return prefix;
}

SideMap enumerateSide(const QString& root, const QString& prefix, const std::atomic<bool>& abort)
{
	SideMap entries;

	scanDirectory(CFileSystemObject{ root }, [&entries, &prefix](const CFileSystemObject& item, bool /*reachedThroughLink*/) {
		const QString path = item.fullAbsolutePath();
		assert_debug_only(path.startsWith(prefix));

		QString relativePath = path.mid(prefix.length());
		if (relativePath.endsWith('/'))
			relativePath.chop(1);

		if (relativePath.isEmpty())
			return; // The root itself, which scanDirectory reports as an item of the scan

		SideEntry entry;
		entry.relativePath = relativePath;
		entry.type = item.type();
		entry.size = item.size();
		entry.isDirectory = item.isDir();
		entries.emplace(std::move(relativePath), std::move(entry));
	}, abort);

	return entries;
}

// Pairs up entries across filesystems that disagree on letter case or on Unicode normalization.
QString foldedKey(const QString& relativePath)
{
	return relativePath.normalized(QString::NormalizationForm_C).toCaseFolded();
}

FolderComparisonEntry pairedEntry(const SideEntry& left, const SideEntry& right, const EntryDifference status)
{
	FolderComparisonEntry entry;
	entry.relativePath = left.relativePath;
	entry.status = status;
	entry.leftType = left.type;
	entry.rightType = right.type;
	entry.leftSize = left.size;
	entry.rightSize = right.size;
	return entry;
}

FolderComparisonEntry singleSidedEntry(const SideEntry& side, const EntryDifference status)
{
	assert_debug_only(status == EntryDifference::OnlyInLeft || status == EntryDifference::OnlyInRight);

	FolderComparisonEntry entry;
	entry.relativePath = side.relativePath;
	entry.status = status;
	if (status == EntryDifference::OnlyInLeft)
	{
		entry.leftType = side.type;
		entry.leftSize = side.size;
	}
	else
	{
		entry.rightType = side.type;
		entry.rightSize = side.size;
	}

	return entry;
}

} // namespace

FolderComparisonResult compareFolders(const QString& leftRoot, const QString& rightRoot, const PairingMode pairingMode,
	const std::atomic<bool>& abort, const std::function<void (int percent)>& onProgress)
{
	FolderComparisonResult result;

	const QString leftPrefix = rootPrefix(leftRoot), rightPrefix = rootPrefix(rightRoot);
	SideMap left = enumerateSide(leftRoot, leftPrefix, abort);
	SideMap right = enumerateSide(rightRoot, rightPrefix, abort);
	if (abort)
	{
		result.aborted = true;
		return result;
	}

	std::vector<std::pair<SideEntry, SideEntry>> contentCandidates;
	uint64_t totalCandidateBytes = 0;

	const auto classifyPair = [&](const SideEntry& l, const SideEntry& r) {
		if (l.isDirectory && r.isDirectory)
			return; // The same shape at this level; anything differing below is an entry in its own right

		if (l.isDirectory != r.isDirectory || l.size != r.size)
		{
			result.differences.push_back(pairedEntry(l, r, EntryDifference::Different));
			return;
		}

		totalCandidateBytes += l.size;
		contentCandidates.emplace_back(l, r);
	};

	for (auto leftIt = left.begin(); leftIt != left.end(); )
	{
		const auto rightIt = right.find(leftIt->first);
		if (rightIt == right.end())
		{
			++leftIt;
			continue;
		}

		classifyPair(leftIt->second, rightIt->second);
		right.erase(rightIt);
		leftIt = left.erase(leftIt);
	}

	// Only what the exact pass left over is folded, so that two entries differing just in case on a case-sensitive
	// filesystem still pair with their true counterparts rather than with each other.
	if (pairingMode == PairingMode::FoldCaseAndNormalization && !left.empty() && !right.empty())
	{
		std::map<QString, QString> foldedRight; // Folded key -> the one exact key it stands for; emptied when several do
		for (const auto& item : right)
		{
			const auto [it, inserted] = foldedRight.emplace(foldedKey(item.first), item.first);
			if (!inserted)
				it->second.clear();
		}

		for (auto leftIt = left.begin(); leftIt != left.end(); )
		{
			const auto foldedIt = foldedRight.find(foldedKey(leftIt->first));
			if (foldedIt == foldedRight.end() || foldedIt->second.isEmpty())
			{
				++leftIt; // No counterpart, or several equally plausible ones - refuse to guess
				continue;
			}

			const auto rightIt = right.find(foldedIt->second);
			assert_debug_only(rightIt != right.end());
			classifyPair(leftIt->second, rightIt->second);
			right.erase(rightIt);
			foldedRight.erase(foldedIt);
			leftIt = left.erase(leftIt);
		}
	}

	for (const auto& item : left)
		result.differences.push_back(singleSidedEntry(item.second, EntryDifference::OnlyInLeft));

	for (const auto& item : right)
		result.differences.push_back(singleSidedEntry(item.second, EntryDifference::OnlyInRight));

	uint64_t comparedBytes = 0;
	for (const auto& candidate : contentCandidates)
	{
		if (abort)
		{
			result.aborted = true;
			break;
		}

		const auto comparison = compareFileContents(leftPrefix % candidate.first.relativePath,
			rightPrefix % candidate.second.relativePath, abort, [&](uint64_t bytesCompared) {
				comparedBytes += bytesCompared;
				if (onProgress)
					onProgress(static_cast<int>(comparedBytes * 100 / totalCandidateBytes)); // Non-zero whenever any bytes get reported
			});

		switch (comparison)
		{
		case ContentComparisonResult::Equal:
			++result.identicalFiles;
			break;
		case ContentComparisonResult::Different:
			result.differences.push_back(pairedEntry(candidate.first, candidate.second, EntryDifference::Different));
			break;
		case ContentComparisonResult::Error:
			result.differences.push_back(pairedEntry(candidate.first, candidate.second, EntryDifference::ComparisonFailed));
			break;
		case ContentComparisonResult::Aborted:
			result.aborted = true;
			break;
		}

		if (result.aborted)
			break;
	}

	std::sort(result.differences.begin(), result.differences.end(),
		[](const FolderComparisonEntry& a, const FolderComparisonEntry& b) { return a.relativePath < b.relativePath; });

	return result;
}
