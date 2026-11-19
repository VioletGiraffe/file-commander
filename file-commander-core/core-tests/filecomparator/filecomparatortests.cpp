#include "filecomparator/cfilecomparator.h"
#include "filecomparator/cfoldercomparison.h"

#include "temptree.h"


// Submodule includes
#include "qtcore_helpers/catch_qt.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <thread>

namespace {

// Polls the comparison as the UI does. A timeout here means it never ended.
template <typename Comparison>
[[nodiscard]] bool finishesInTime(Comparison& comparison)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
	while (!comparison.finished())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::yield();
	}
	return true;
}

// Distinct byte values, so a shifted or misread block cannot pass for the expected one
[[nodiscard]] QByteArray patternedData(const qsizetype size)
{
	QByteArray data(size, Qt::Uninitialized);
	for (qsizetype i = 0; i < size; ++i)
		data[i] = static_cast<char>((i * 31 + 7) % 251);
	return data;
}

[[nodiscard]] ContentComparisonResult comparisonResult(const QString& pathA, const QString& pathB)
{
	CFileComparator comparator{ pathA, pathB };
	REQUIRE(finishesInTime(comparator));
	REQUIRE(comparator.result().has_value());
	return *comparator.result();
}

} // namespace

TEST_CASE("CFileComparator: equal files of several blocks are Equal, and the progress ends at 100%", "[filecomparator]")
{
	TempTree tree;
	const QByteArray data = patternedData(3 * 1024 * 1024 + 1);
	const QString a = tree.makeFile(QSL("a"), data), b = tree.makeFile(QSL("b"), data);

	CFileComparator comparator{ a, b };
	REQUIRE(finishesInTime(comparator));
	REQUIRE(comparator.result().has_value());
	CHECK(*comparator.result() == ContentComparisonResult::Equal);
	CHECK(comparator.percent() == 100);
}

TEST_CASE("CFileComparator: two empty files are Equal", "[filecomparator]")
{
	TempTree tree;
	CHECK(comparisonResult(tree.makeFile(QSL("a")), tree.makeFile(QSL("b"))) == ContentComparisonResult::Equal);
}

TEST_CASE("CFileComparator: files that differ in their last byte, or in size, are Different", "[filecomparator]")
{
	TempTree tree;
	const QByteArray data = patternedData(1024 * 1024 + 1);
	QByteArray changed = data;
	changed.back() = static_cast<char>(~changed.back());
	const QString a = tree.makeFile(QSL("a"), data);

	CHECK(comparisonResult(a, tree.makeFile(QSL("changed"), changed)) == ContentComparisonResult::Different);
	CHECK(comparisonResult(a, tree.makeFile(QSL("shorter"), data.first(data.size() - 1))) == ContentComparisonResult::Different);
}

TEST_CASE("CFileComparator: a file that does not exist is an Error on either side", "[filecomparator]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("file"), patternedData(10));
	const QString missing = tree.path(QSL("missing"));

	CHECK(comparisonResult(file, missing) == ContentComparisonResult::Error);
	CHECK(comparisonResult(missing, file) == ContentComparisonResult::Error);
}

TEST_CASE("CFileComparator: destroying it while it compares returns", "[filecomparator]")
{
	TempTree tree;
	const QByteArray data = patternedData(16 * 1024 * 1024);
	const QString a = tree.makeFile(QSL("a"), data), b = tree.makeFile(QSL("b"), data);

	{
		const CFileComparator comparator{ a, b };
	}

	SUCCEED("The destructor cancelled the comparison and joined it");
}

TEST_CASE("filesToCompare: the other side's current file, or else the file of the same name in its folder", "[filecomparator]")
{
	TempTree tree;
	tree.makeDir(QSL("left"));
	tree.makeDir(QSL("right"));
	const QString current = tree.makeFile(QSL("left/a.txt"), patternedData(10));
	const QString sameName = tree.makeFile(QSL("right/a.txt"), patternedData(10));
	const QString otherFile = tree.makeFile(QSL("right/b.txt"), patternedData(10));
	const QString otherFolder = tree.makeDir(QSL("right/sub"));

	SECTION("The other side's current item is a file")
	{
		const auto files = filesToCompare(CFileSystemObject{ current }, CFileSystemObject{ otherFile }, tree.path(QSL("right")));
		REQUIRE(files.has_value());
		CHECK(files->pathA == CFileSystemObject{ current }.fullAbsolutePath());
		CHECK(files->pathB == CFileSystemObject{ otherFile }.fullAbsolutePath());
	}

	SECTION("The other side's current item is a folder")
	{
		const auto files = filesToCompare(CFileSystemObject{ current }, CFileSystemObject{ otherFolder }, tree.path(QSL("right")));
		REQUIRE(files.has_value());
		CHECK(QFileInfo{ files->pathB }.canonicalFilePath() == QFileInfo{ sameName }.canonicalFilePath());
	}
}

TEST_CASE("filesToCompare: a message instead when the current item is not a file, a file is missing, or the sizes differ", "[filecomparator]")
{
	TempTree tree;
	tree.makeDir(QSL("left"));
	tree.makeDir(QSL("right"));
	const QString current = tree.makeFile(QSL("left/a.txt"), patternedData(10));
	const QString longer = tree.makeFile(QSL("right/longer.txt"), patternedData(11));
	const QString folder = tree.makeDir(QSL("left/sub"));

	const auto messageFor = [&tree](const QString& currentPath, const CFileSystemObject& otherCurrent) {
		const auto files = filesToCompare(CFileSystemObject{ currentPath }, otherCurrent, tree.path(QSL("right")));
		REQUIRE_FALSE(files.has_value());
		CHECK(files.error().severity == MessageSeverity::Warning);
		return files.error().text;
	};

	CHECK(messageFor(folder, CFileSystemObject{ longer }).startsWith(QSL("The selected item is not a file:")));
	// The other side's current item is not a file, and its folder has no a.txt
	CHECK(messageFor(current, CFileSystemObject{ tree.path(QSL("right")) }).contains(QSL("doesn't exist")));
	CHECK(messageFor(current, CFileSystemObject{ longer }).startsWith(QSL("Files have different sizes:")));
}

TEST_CASE("fileComparisonResultMessage: identical files are information, different ones a warning, unreadable ones an error", "[filecomparator]")
{
	const FilesToCompare files{ .pathA = QSL("/a"), .pathB = QSL("/b") };
	CHECK(fileComparisonResultMessage(ContentComparisonResult::Equal, files).severity == MessageSeverity::Information);
	CHECK(fileComparisonResultMessage(ContentComparisonResult::Equal, files).text.contains(QSL("IDENTICAL")));
	CHECK(fileComparisonResultMessage(ContentComparisonResult::Different, files).severity == MessageSeverity::Warning);
	CHECK(fileComparisonResultMessage(ContentComparisonResult::Different, files).text.contains(QSL("DIFFERENT")));
	CHECK(fileComparisonResultMessage(ContentComparisonResult::Error, files).severity == MessageSeverity::Error);
}

TEST_CASE("CFolderComparison: identical folders have no differences, and the summary says so", "[filecomparator]")
{
	TempTree tree;
	for (const QString& root : { QSL("left"), QSL("right") })
	{
		tree.makeDir(root);
		tree.makeFile(root % QSL("/a.txt"), patternedData(100));
		tree.makeFile(root % QSL("/b.txt"), patternedData(200));
	}

	CFolderComparison comparison{ tree.path(QSL("left")), tree.path(QSL("right")) };
	REQUIRE(finishesInTime(comparison));
	REQUIRE(comparison.result().has_value());
	CHECK(comparison.result()->differences.empty());
	CHECK(comparison.result()->identicalFiles == 2);
	CHECK(folderComparisonSummary(*comparison.result()) == QSL("The folders are identical: 2 files compared byte by byte."));
}

TEST_CASE("CFolderComparison: an entry is located in the panel that holds it", "[filecomparator]")
{
	TempTree tree;
	tree.makeDir(QSL("left"));
	tree.makeDir(QSL("right"));
	tree.makeFile(QSL("left/only-left.txt"), patternedData(10));
	tree.makeFile(QSL("right/only-right.txt"), patternedData(10));
	tree.makeFile(QSL("left/both.txt"), patternedData(10));
	tree.makeFile(QSL("right/both.txt"), patternedData(11));

	CFolderComparison comparison{ tree.path(QSL("left")), tree.path(QSL("right")) };
	REQUIRE(finishesInTime(comparison));
	REQUIRE(comparison.result().has_value());

	const auto& differences = comparison.result()->differences;
	const auto entryAt = [&differences](const QString& relativePath) {
		const auto it = std::ranges::find(differences, relativePath, &FolderComparisonEntry::relativePath);
		REQUIRE(it != differences.end());
		return *it;
	};

	const FolderComparisonEntry onlyLeft = entryAt(QSL("only-left.txt")), onlyRight = entryAt(QSL("only-right.txt")), both = entryAt(QSL("both.txt"));
	CHECK(onlyLeft.status == EntryDifference::OnlyInLeft);
	CHECK(onlyRight.status == EntryDifference::OnlyInRight);
	CHECK(both.status == EntryDifference::Different);
	CHECK(folderComparisonStatusText(onlyRight.status) == QSL("Only on the right"));

	CHECK(comparison.locationOf(onlyLeft).panel == Panel::LeftPanel);
	CHECK(comparison.locationOf(onlyLeft).absolutePath == tree.path(QSL("left/only-left.txt")));
	CHECK(comparison.locationOf(onlyRight).panel == Panel::RightPanel);
	CHECK(comparison.locationOf(onlyRight).absolutePath == tree.path(QSL("right/only-right.txt")));
	CHECK(comparison.locationOf(both).panel == Panel::LeftPanel); // Present on both sides: the left one
}
