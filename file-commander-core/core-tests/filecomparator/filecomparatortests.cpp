#include "filecomparator/cfilecomparator.h"
#include "filecomparator/cfoldercomparison.h"

#include "temptree.h"


// Submodule includes
#include "qtcore_helpers/catch_qt.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"


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
