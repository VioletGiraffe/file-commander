// WP11: the synchronous inline-rename command and its matrix, distinct from the batch collision policy.

#include "fileoperations/inlinerename.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#endif

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
#else
constexpr NativeErrorCode ioFailureCode = EIO;
#endif

} // namespace

TEST_CASE("inline rename: no-op and straight rename", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/file.bin", patternedContents(400));

	SECTION("the same spelling is nothing to do")
	{
		const auto result = inlineRename(ep(base % "/file.bin"), QStringLiteral("file.bin"), false);
		CHECK(result.status == InlineRenameStatus::NothingToDo);
		CHECK(!entryAbsent(base % "/file.bin"));
	}

	SECTION("a file renamed to an absent name is renamed")
	{
		const auto result = inlineRename(ep(base % "/file.bin"), QStringLiteral("renamed.bin"), false);
		CHECK(result.status == InlineRenameStatus::Renamed);
		CHECK(entryAbsent(base % "/file.bin"));
		CHECK(readFileContents(base % "/renamed.bin") == patternedContents(400));
	}

	SECTION("an invalid name is rejected")
	{
		for (const QString& bad : { QString{}, QStringLiteral("a/b.bin"), QStringLiteral(".."), QStringLiteral(".") })
		{
			INFO("name: '" << bad.toStdString() << "'");
			CHECK(inlineRename(ep(base % "/file.bin"), bad, false).status == InlineRenameStatus::RejectedInvalidName);
		}
		CHECK(!entryAbsent(base % "/file.bin")); // Nothing touched
	}
}

TEST_CASE("inline rename: a case-only change is performed", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/file.bin", patternedContents(120));

	const auto result = inlineRename(ep(base % "/file.bin"), QStringLiteral("FILE.BIN"), false);
	CHECK(result.status == InlineRenameStatus::Renamed);

	// The entry now carries the new-case spelling, whichever way the filesystem stores case.
	const QStringList names = QDir{ base }.entryList(QDir::Files);
	CHECK(names.contains(QStringLiteral("FILE.BIN")));
	CHECK(!names.contains(QStringLiteral("file.bin")));
}

TEST_CASE("inline rename: a file-like collision needs confirmation, then replaces", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/src.bin", patternedContents(700));
	writeTestFile(base % "/dst.bin", patternedContents(50));

	SECTION("without confirmation, the destination is preserved")
	{
		const auto result = inlineRename(ep(base % "/src.bin"), QStringLiteral("dst.bin"), false);
		CHECK(result.status == InlineRenameStatus::ReplacementRequired);
		CHECK(result.sourceKind == OperationEntryKind::RegularFile);
		CHECK(result.destinationKind == OperationEntryKind::RegularFile);
		CHECK(readFileContents(base % "/dst.bin") == patternedContents(50)); // Untouched
		CHECK(!entryAbsent(base % "/src.bin"));
	}

	SECTION("with confirmation, it is one atomic replacement")
	{
		const auto result = inlineRename(ep(base % "/src.bin"), QStringLiteral("dst.bin"), true);
		CHECK(result.status == InlineRenameStatus::Renamed);
		CHECK(entryAbsent(base % "/src.bin"));
		CHECK(readFileContents(base % "/dst.bin") == patternedContents(700));
	}
}

TEST_CASE("inline rename: type mismatches and directory collisions are rejected", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/file.bin", patternedContents(90));
	REQUIRE(QDir{}.mkpath(base % "/folder"));
	REQUIRE(QDir{}.mkpath(base % "/otherFolder"));

	SECTION("a file cannot replace a real directory")
	{
		const auto result = inlineRename(ep(base % "/file.bin"), QStringLiteral("folder"), false);
		CHECK(result.status == InlineRenameStatus::Rejected);
		CHECK(result.sourceKind == OperationEntryKind::RegularFile);
		CHECK(result.destinationKind == OperationEntryKind::Directory);
		CHECK(!entryAbsent(base % "/file.bin"));
	}

	SECTION("a directory never replaces an occupied file")
	{
		const auto result = inlineRename(ep(base % "/folder"), QStringLiteral("file.bin"), false);
		CHECK(result.status == InlineRenameStatus::Rejected);
		CHECK(result.sourceKind == OperationEntryKind::Directory);
		CHECK(result.destinationKind == OperationEntryKind::RegularFile);
	}

	SECTION("a directory never merges into an occupied directory")
	{
		const auto result = inlineRename(ep(base % "/folder"), QStringLiteral("otherFolder"), false);
		CHECK(result.status == InlineRenameStatus::Rejected);
	}
}

TEST_CASE("inline rename: a directory-link destination is rejected like a folder", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/src.bin", patternedContents(300));
	REQUIRE(QDir{}.mkpath(base % "/realTarget"));
	REQUIRE(createDirectoryLink(base % "/realTarget", base % "/aLink"));

	// Inline rename cannot atomically replace a directory link (Windows refuses it), so it is rejected.
	const auto result = inlineRename(ep(base % "/src.bin"), QStringLiteral("aLink"), false);
	CHECK(result.status == InlineRenameStatus::Rejected);
	CHECK(result.destinationKind == OperationEntryKind::DirectoryLink);
	CHECK(!entryAbsent(base % "/src.bin"));
}

TEST_CASE("inline rename: a directory-like source rejects an occupied destination", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	REQUIRE(QDir{}.mkpath(base % "/realTarget"));
	REQUIRE(createDirectoryLink(base % "/realTarget", base % "/dirLink"));
	writeTestFile(base % "/occupied.bin", patternedContents(30));

	const auto result = inlineRename(ep(base % "/dirLink"), QStringLiteral("occupied.bin"), false);
	CHECK(result.status == InlineRenameStatus::Rejected);
	CHECK(result.sourceKind == OperationEntryKind::DirectoryLink);
}

TEST_CASE("inline rename: a native failure surfaces a structured error", "[inlinerename]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/file.bin", patternedContents(60));

	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RenameEntry_Native, ioFailureCode);

	const auto result = inlineRename(ep(base % "/file.bin"), QStringLiteral("renamed.bin"), false);
	CHECK(result.status == InlineRenameStatus::Failed);
	REQUIRE(result.failure.has_value());
	CHECK(result.failure->action == FailedAction::RenameEntry);
	CHECK(!entryAbsent(base % "/file.bin")); // The forced failure left the source in place
}
