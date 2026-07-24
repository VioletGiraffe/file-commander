#include "filecomparator/foldercomparison.h"

#include "compiler/compiler_warnings_control.h"
#include "qtcore_helpers/qstring_helpers.hpp"

#include "3rdparty/catch2/catch.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

void writeFile(const QString& path, const QByteArray& contents)
{
	REQUIRE(QDir{}.mkpath(QFileInfo{ path }.absolutePath()));
	QFile file{ path };
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

const FolderComparisonEntry* findEntry(const FolderComparisonResult& result, const QString& relativePath)
{
	const auto it = std::find_if(result.differences.begin(), result.differences.end(),
		[&relativePath](const FolderComparisonEntry& entry) { return entry.relativePath == relativePath; });

	return it != result.differences.end() ? &(*it) : nullptr;
}

} // namespace

TEST_CASE("compareFolders: identical trees", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	const auto populate = [](const QString& root) {
		writeFile(root % "/a.bin", QByteArray(1000, 'a'));
		writeFile(root % "/sub/b.bin", QByteArray(2000, 'b'));
		writeFile(root % "/sub/deeper/empty.bin", QByteArray{});
	};

	populate(left.path());
	populate(right.path());

	const auto result = compareFolders(left.path(), right.path());

	CHECK(result.differences.empty());
	CHECK(result.identicalFiles == 3);
	CHECK_FALSE(result.aborted);
}

TEST_CASE("compareFolders: entries present on one side only", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	writeFile(left.path() % "/shared.bin", QByteArray(10, 'x'));
	writeFile(right.path() % "/shared.bin", QByteArray(10, 'x'));
	writeFile(left.path() % "/onlyleft/nested.bin", QByteArray(5, 'l'));
	writeFile(right.path() % "/onlyright.bin", QByteArray(5, 'r'));

	const auto result = compareFolders(left.path(), right.path());

	CHECK(result.identicalFiles == 1);
	CHECK(result.differences.size() == 3); // The unique directory, its one descendant, and the unique file

	const auto* uniqueDir = findEntry(result, QSL("onlyleft"));
	REQUIRE(uniqueDir != nullptr);
	CHECK(uniqueDir->status == EntryDifference::OnlyInLeft);
	CHECK(uniqueDir->leftType == Directory);

	const auto* nested = findEntry(result, QSL("onlyleft/nested.bin"));
	REQUIRE(nested != nullptr);
	CHECK(nested->status == EntryDifference::OnlyInLeft);
	CHECK(nested->leftSize == 5);

	const auto* uniqueFile = findEntry(result, QSL("onlyright.bin"));
	REQUIRE(uniqueFile != nullptr);
	CHECK(uniqueFile->status == EntryDifference::OnlyInRight);
	CHECK(uniqueFile->rightSize == 5);
	CHECK(uniqueFile->leftType == UnknownType);
	CHECK(uniqueFile->leftSize == 0);
}

TEST_CASE("compareFolders: files that differ", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	writeFile(left.path() % "/samesize.bin", QByteArray(1000, 'a'));
	writeFile(right.path() % "/samesize.bin", QByteArray(1000, 'b'));
	writeFile(left.path() % "/othersize.bin", QByteArray(1000, 'c'));
	writeFile(right.path() % "/othersize.bin", QByteArray(999, 'c'));

	const auto result = compareFolders(left.path(), right.path());

	CHECK(result.identicalFiles == 0);
	CHECK(result.differences.size() == 2);

	const auto* sameSize = findEntry(result, QSL("samesize.bin"));
	REQUIRE(sameSize != nullptr);
	CHECK(sameSize->status == EntryDifference::Different);
	CHECK(sameSize->leftSize == 1000);
	CHECK(sameSize->rightSize == 1000);

	const auto* otherSize = findEntry(result, QSL("othersize.bin"));
	REQUIRE(otherSize != nullptr);
	CHECK(otherSize->status == EntryDifference::Different);
	CHECK(otherSize->rightSize == 999);
}

TEST_CASE("compareFolders: a file against a directory of the same name", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	writeFile(left.path() % "/item", QByteArray(10, 'x'));
	writeFile(right.path() % "/item/inner.bin", QByteArray(3, 'y'));

	const auto result = compareFolders(left.path(), right.path());

	const auto* item = findEntry(result, QSL("item"));
	REQUIRE(item != nullptr);
	CHECK(item->status == EntryDifference::Different);
	CHECK(item->leftType == File);
	CHECK(item->rightType == Directory);

	const auto* inner = findEntry(result, QSL("item/inner.bin"));
	REQUIRE(inner != nullptr);
	CHECK(inner->status == EntryDifference::OnlyInRight);
}

TEST_CASE("compareFolders: names differing only in letter case are paired", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	writeFile(left.path() % "/File.txt", QByteArray(50, 'z'));
	writeFile(right.path() % "/file.txt", QByteArray(50, 'z'));

	const auto result = compareFolders(left.path(), right.path());

	CHECK(result.differences.empty());
	CHECK(result.identicalFiles == 1);
}

TEST_CASE("compareFolders: progress rises to completion", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	const auto populate = [](const QString& root) {
		writeFile(root % "/first.bin", QByteArray(4000, 'a'));
		writeFile(root % "/second.bin", QByteArray(4000, 'b'));
	};

	populate(left.path());
	populate(right.path());

	int lastPercent = -1;
	bool monotonic = true;
	const auto result = compareFolders(left.path(), right.path(), std::atomic<bool>{ false }, [&](int percent) {
		monotonic = monotonic && percent >= lastPercent;
		lastPercent = percent;
	});

	CHECK(result.identicalFiles == 2);
	CHECK(monotonic);
	CHECK(lastPercent == 100);
}

TEST_CASE("compareFolders: an aborted comparison says so", "[foldercomparison]")
{
	QTemporaryDir left, right;
	REQUIRE(left.isValid());
	REQUIRE(right.isValid());

	writeFile(left.path() % "/a.bin", QByteArray(100, 'a'));
	writeFile(right.path() % "/a.bin", QByteArray(100, 'a'));

	const std::atomic<bool> abort{ true };
	const auto result = compareFolders(left.path(), right.path(), abort);

	CHECK(result.aborted);
	CHECK(result.identicalFiles == 0);
}
