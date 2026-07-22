// WP1 foundation: entry inspection and identity, stateless mutations, native error classification,
// and the deterministic fault points immediately around the native calls.

#include "fileoperations/cfilesystemmutator.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h> // mkfifo
#endif

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

//
// Entry inspection
//

TEST_CASE("inspectEntry: absent entry, regular file, directory", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	CHECK(entryAbsent(base % "/nothing_here"));

	const QString filePath = base % "/file.bin";
	writeTestFile(filePath, QByteArray(3000, 'x'));
	const auto fileSnapshot = snapshotOf(filePath);
	CHECK(fileSnapshot.kind == OperationEntryKind::RegularFile);
	CHECK(fileSnapshot.size == 3000);

	REQUIRE(QDir{}.mkpath(base % "/subdir"));
	const auto dirSnapshot = snapshotOf(base % "/subdir");
	CHECK(dirSnapshot.kind == OperationEntryKind::Directory);
	CHECK(dirSnapshot.size == 0);
}

TEST_CASE("inspectEntry: directory link, broken directory link", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/target"));
	const QString linkPath = base % "/dirlink";
	REQUIRE(createDirectoryLink(base % "/target", linkPath));
	CHECK(snapshotOf(linkPath).kind == OperationEntryKind::DirectoryLink);

	// A broken directory link remains an existing entry; the entry's own directory kind must be preserved
	// on Windows, where removing it later takes the directory primitive.
	REQUIRE(QDir{}.rmdir(base % "/target"));
#ifdef _WIN32
	CHECK(snapshotOf(linkPath).kind == OperationEntryKind::DirectoryLink);
#else
	CHECK(snapshotOf(linkPath).kind == OperationEntryKind::FileLink); // A POSIX symlink entry carries no target type
#endif
}

#ifndef _WIN32
TEST_CASE("inspectEntry: file link, broken link, link to non-regular target", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString targetPath = base % "/target.bin";
	writeTestFile(targetPath, QByteArray(4000, 'T'));
	REQUIRE(QFile::link(targetPath, base % "/filelink"));
	const auto linkSnapshot = snapshotOf(base % "/filelink");
	CHECK(linkSnapshot.kind == OperationEntryKind::FileLink);
	CHECK(linkSnapshot.size == 4000); // Followed-target size

	REQUIRE(QFile::link(base % "/no_such_target", base % "/brokenlink"));
	const auto brokenSnapshot = snapshotOf(base % "/brokenlink"); // Broken links are existing entries
	CHECK(brokenSnapshot.kind == OperationEntryKind::FileLink);
	CHECK(brokenSnapshot.size == 0);

	REQUIRE(::mkfifo(QFile::encodeName(base % "/fifo").constData(), 0700) == 0);
	CHECK(snapshotOf(base % "/fifo").kind == OperationEntryKind::Other);
	REQUIRE(QFile::link(base % "/fifo", base % "/fifolink"));
	CHECK(snapshotOf(base % "/fifolink").kind == OperationEntryKind::Other); // Never streamed as a regular file
}
#endif

//
// Identity and same-object checks
//

TEST_CASE("checkSameEntry: hard-link aliases, distinct files, absent entries", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString filePath = base % "/file.bin";
	writeTestFile(filePath, QByteArray(100, 'a'));
	const QString aliasPath = base % "/alias.bin";
	REQUIRE(createHardLink(filePath, aliasPath));

	const auto sameVerdict = checkSameEntry(ep(filePath), ep(aliasPath), thin_io::link_behavior::do_not_follow);
	REQUIRE(sameVerdict.has_value());
	CHECK(*sameVerdict == SameEntryVerdict::Same);

	writeTestFile(base % "/other.bin", QByteArray(100, 'b'));
	const auto differentVerdict = checkSameEntry(ep(filePath), ep(base % "/other.bin"), thin_io::link_behavior::do_not_follow);
	REQUIRE(differentVerdict.has_value());
	CHECK(*differentVerdict == SameEntryVerdict::Different);

	const auto absentVerdict = checkSameEntry(ep(filePath), ep(base % "/missing"), thin_io::link_behavior::do_not_follow);
	REQUIRE(absentVerdict.has_value());
	CHECK(*absentVerdict == SameEntryVerdict::Different);
}

#ifndef _WIN32
TEST_CASE("checkSameEntry: link behavior decides whether a symlink aliases its target", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString targetPath = base % "/target.bin";
	writeTestFile(targetPath, QByteArray(100, 'a'));
	const QString linkPath = base % "/link";
	REQUIRE(QFile::link(targetPath, linkPath));

	const auto followed = checkSameEntry(ep(linkPath), ep(targetPath), thin_io::link_behavior::follow);
	REQUIRE(followed.has_value());
	CHECK(*followed == SameEntryVerdict::Same);

	const auto notFollowed = checkSameEntry(ep(linkPath), ep(targetPath), thin_io::link_behavior::do_not_follow);
	REQUIRE(notFollowed.has_value());
	CHECK(*notFollowed == SameEntryVerdict::Different);
}
#endif

//
// renameEntry
//

TEST_CASE("renameEntry: require-absent success, collision, and missing source", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents(2000, 'r');
	writeTestFile(base % "/src.bin", contents);

	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/dst.bin"), ReplacementMode::RequireAbsent).has_value());
	CHECK(entryAbsent(base % "/src.bin"));
	CHECK(readFileContents(base % "/dst.bin") == contents);

	writeTestFile(base % "/src2.bin", QByteArray(10, 'y'));
	const auto collision = CFileSystemMutator::renameEntry(ep(base % "/src2.bin"), ep(base % "/dst.bin"), ReplacementMode::RequireAbsent);
	REQUIRE(!collision.has_value());
	CHECK(collision.error().category == FileErrorCategory::AlreadyExists);
	CHECK(readFileContents(base % "/dst.bin") == contents); // Both entries intact
	CHECK(!entryAbsent(base % "/src2.bin"));

	const auto missing = CFileSystemMutator::renameEntry(ep(base % "/no_such"), ep(base % "/whatever"), ReplacementMode::RequireAbsent);
	REQUIRE(!missing.has_value());
	CHECK(missing.error().category == FileErrorCategory::NotFound);
	CHECK(!missing.error().diagnostic.isEmpty());
}

TEST_CASE("renameEntry: replace-existing-file replaces atomically", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray newContents(2000, 'n');
	writeTestFile(base % "/src.bin", newContents);
	writeTestFile(base % "/dst.bin", QByteArray(3000, 'o'));

	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/dst.bin"), ReplacementMode::ReplaceExistingFile).has_value());
	CHECK(entryAbsent(base % "/src.bin"));
	CHECK(readFileContents(base % "/dst.bin") == newContents);
}

TEST_CASE("renameEntry: replacing a hard-link pathname does not change the other alias", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray oldContents(1000, 'o');
	const QByteArray newContents(1000, 'n');
	writeTestFile(base % "/original.bin", oldContents);
	REQUIRE(createHardLink(base % "/original.bin", base % "/alias.bin"));
	writeTestFile(base % "/src.bin", newContents);

	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/alias.bin"), ReplacementMode::ReplaceExistingFile).has_value());
	CHECK(readFileContents(base % "/alias.bin") == newContents);
	CHECK(readFileContents(base % "/original.bin") == oldContents);
}

#ifndef _WIN32
TEST_CASE("renameEntry: replacing a symlink pathname does not change its target", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray targetContents(1000, 't');
	const QByteArray newContents(1000, 'n');
	writeTestFile(base % "/target.bin", targetContents);
	REQUIRE(QFile::link(base % "/target.bin", base % "/link"));
	writeTestFile(base % "/src.bin", newContents);

	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/link"), ReplacementMode::ReplaceExistingFile).has_value());
	CHECK(readFileContents(base % "/link") == newContents); // The pathname now names a regular file
	CHECK(snapshotOf(base % "/link").kind == OperationEntryKind::RegularFile);
	CHECK(readFileContents(base % "/target.bin") == targetContents);
}

TEST_CASE("renameEntry: a symlink source is renamed as the link entry", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/target.bin", QByteArray(100, 't'));
	REQUIRE(QFile::link(base % "/target.bin", base % "/link"));

	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/link"), ep(base % "/moved_link"), ReplacementMode::RequireAbsent).has_value());
	CHECK(entryAbsent(base % "/link"));
	CHECK(snapshotOf(base % "/moved_link").kind == OperationEntryKind::FileLink);
	CHECK(!entryAbsent(base % "/target.bin"));
}
#endif

TEST_CASE("renameEntry: directory rename and directory-link rename", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/dir"));
	writeTestFile(base % "/dir/inner.bin", QByteArray(100, 'i'));
	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/dir"), ep(base % "/renamed_dir"), ReplacementMode::RequireAbsent).has_value());
	CHECK(readFileContents(base % "/renamed_dir/inner.bin") == QByteArray(100, 'i'));

	REQUIRE(createDirectoryLink(base % "/renamed_dir", base % "/dirlink"));
	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/dirlink"), ep(base % "/moved_dirlink"), ReplacementMode::RequireAbsent).has_value());
	CHECK(snapshotOf(base % "/moved_dirlink").kind == OperationEntryKind::DirectoryLink);
	CHECK(readFileContents(base % "/renamed_dir/inner.bin") == QByteArray(100, 'i')); // Target untouched
}

TEST_CASE("renameEntry: case-only rename", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/case.txt", QByteArray(10, 'c'));
	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/case.txt"), ep(base % "/CASE.txt"), ReplacementMode::RequireAbsent).has_value());
	CHECK(QDir{ base }.entryList(QDir::Files).contains(QStringLiteral("CASE.txt"))); // The exact new spelling

	REQUIRE(QDir{}.mkpath(base % "/folder"));
	REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/folder"), ep(base % "/FOLDER"), ReplacementMode::RequireAbsent).has_value());
	CHECK(QDir{ base }.entryList(QDir::Dirs).contains(QStringLiteral("FOLDER")));
}

TEST_CASE("renameEntry: directory replacement is rejected", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/file.bin", QByteArray(10, 'f'));
	REQUIRE(QDir{}.mkpath(base % "/dir"));
	REQUIRE(QDir{}.mkpath(base % "/emptydir"));

	// A file cannot replace a directory
	CHECK(!CFileSystemMutator::renameEntry(ep(base % "/file.bin"), ep(base % "/dir"), ReplacementMode::ReplaceExistingFile).has_value());
	CHECK(!entryAbsent(base % "/file.bin"));
	CHECK(snapshotOf(base % "/dir").kind == OperationEntryKind::Directory);

	// Neither can a directory, even an empty destination one (POSIX rename() would silently allow this)
	REQUIRE(QDir{}.mkpath(base % "/srcdir"));
	writeTestFile(base % "/srcdir/inner.bin", QByteArray(5, 'i'));
	CHECK(!CFileSystemMutator::renameEntry(ep(base % "/srcdir"), ep(base % "/emptydir"), ReplacementMode::ReplaceExistingFile).has_value());
	CHECK(snapshotOf(base % "/emptydir").kind == OperationEntryKind::Directory);
	CHECK(readFileContents(base % "/srcdir/inner.bin") == QByteArray(5, 'i'));
}

#ifndef _WIN32
TEST_CASE("renameEntry: unsupported exclusive rename degrades to recheck-then-rename", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	const QByteArray contents(100, 'd');

	{
		// Destination absent at the recheck: the degraded path publishes via plain rename.
		CFaultHookScope scope;
		scope.forceNativeError(Point::RenameEntry_Native, EINVAL);
		writeTestFile(base % "/src.bin", contents);
		REQUIRE(CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/dst.bin"), ReplacementMode::RequireAbsent).has_value());
		CHECK(entryAbsent(base % "/src.bin"));
		CHECK(readFileContents(base % "/dst.bin") == contents);
	}

	{
		// An entry is present at the recheck: AlreadyExists, re-entering resolution; nothing is replaced.
		CFaultHookScope scope;
		scope.forceNativeError(Point::RenameEntry_Native, ENOTSUP);
		writeTestFile(base % "/src2.bin", QByteArray(100, 's'));
		const auto result = CFileSystemMutator::renameEntry(ep(base % "/src2.bin"), ep(base % "/dst.bin"), ReplacementMode::RequireAbsent);
		REQUIRE(!result.has_value());
		CHECK(result.error().category == FileErrorCategory::AlreadyExists);
		CHECK(readFileContents(base % "/dst.bin") == contents);
		CHECK(!entryAbsent(base % "/src2.bin"));
	}
}
#endif

TEST_CASE("renameEntry: native error classification through forced faults", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	struct CodeExpectation
	{
		NativeErrorCode code;
		FileErrorCategory category;
	};
#ifdef _WIN32
	constexpr CodeExpectation table[] {
		{ ERROR_NOT_SAME_DEVICE, FileErrorCategory::CrossDevice },
		{ ERROR_DISK_FULL, FileErrorCategory::NotEnoughSpace },
		{ ERROR_ACCESS_DENIED, FileErrorCategory::PermissionDenied },
		{ ERROR_WRITE_PROTECT, FileErrorCategory::ReadOnly },
		{ ERROR_NOT_SUPPORTED, FileErrorCategory::Unsupported },
		{ ERROR_GEN_FAILURE, FileErrorCategory::IoFailure },
	};
#else
	constexpr CodeExpectation table[] {
		{ EXDEV, FileErrorCategory::CrossDevice },
		{ ENOSPC, FileErrorCategory::NotEnoughSpace },
		{ EACCES, FileErrorCategory::PermissionDenied },
		{ EROFS, FileErrorCategory::ReadOnly },
		{ ENOTSUP, FileErrorCategory::Unsupported }, // Unsupported-degradation applies only to require-absent mode
		{ EIO, FileErrorCategory::IoFailure },
	};
#endif

	writeTestFile(base % "/src.bin", QByteArray(10, 's'));
	writeTestFile(base % "/dst.bin", QByteArray(10, 'd'));

	for (const auto& expectation : table)
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::RenameEntry_Native, expectation.code);
		const auto result = CFileSystemMutator::renameEntry(ep(base % "/src.bin"), ep(base % "/dst.bin"), ReplacementMode::ReplaceExistingFile);
		REQUIRE(!result.has_value());
		CHECK(result.error().category == expectation.category);
		CHECK(result.error().nativeCode == expectation.code);
		CHECK(!result.error().diagnostic.isEmpty());
		CHECK(!entryAbsent(base % "/src.bin")); // The forced fault replaced the native call; nothing moved
	}
}

//
// removeEntry
//

TEST_CASE("removeEntry: file, empty directory, non-empty directory", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/file.bin", QByteArray(10, 'f'));
	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/file.bin")).has_value());
	CHECK(entryAbsent(base % "/file.bin"));

	REQUIRE(QDir{}.mkpath(base % "/emptydir"));
	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/emptydir")).has_value());
	CHECK(entryAbsent(base % "/emptydir"));

	REQUIRE(QDir{}.mkpath(base % "/nonempty"));
	writeTestFile(base % "/nonempty/inner.bin", QByteArray(10, 'i'));
	CHECK(!CFileSystemMutator::removeEntry(snapshotOf(base % "/nonempty")).has_value());
	CHECK(readFileContents(base % "/nonempty/inner.bin") == QByteArray(10, 'i'));
}

TEST_CASE("removeEntry: directory link is unlinked without touching the target", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/target"));
	writeTestFile(base % "/target/inner.bin", QByteArray(10, 'i'));
	REQUIRE(createDirectoryLink(base % "/target", base % "/dirlink"));

	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/dirlink")).has_value());
	CHECK(entryAbsent(base % "/dirlink"));
	CHECK(readFileContents(base % "/target/inner.bin") == QByteArray(10, 'i'));
}

TEST_CASE("removeEntry: broken directory link", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/target"));
	REQUIRE(createDirectoryLink(base % "/target", base % "/dirlink"));
	REQUIRE(QDir{}.rmdir(base % "/target"));

	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/dirlink")).has_value());
	CHECK(entryAbsent(base % "/dirlink"));
}

#ifndef _WIN32
TEST_CASE("removeEntry: file link, broken link, and FIFO are unlinked as entries", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/target.bin", QByteArray(10, 't'));
	REQUIRE(QFile::link(base % "/target.bin", base % "/link"));
	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/link")).has_value());
	CHECK(entryAbsent(base % "/link"));
	CHECK(readFileContents(base % "/target.bin") == QByteArray(10, 't')); // Target untouched

	REQUIRE(QFile::link(base % "/no_such", base % "/broken"));
	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/broken")).has_value());
	CHECK(entryAbsent(base % "/broken"));

	REQUIRE(::mkfifo(QFile::encodeName(base % "/fifo").constData(), 0700) == 0);
	REQUIRE(CFileSystemMutator::removeEntry(snapshotOf(base % "/fifo")).has_value());
	CHECK(entryAbsent(base % "/fifo"));
}
#endif

TEST_CASE("removeEntry: forced fault classifies and mutates nothing", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/file.bin", QByteArray(10, 'f'));

	CFaultHookScope scope;
#ifdef _WIN32
	scope.forceNativeError(Point::RemoveEntry_Native, ERROR_ACCESS_DENIED);
#else
	scope.forceNativeError(Point::RemoveEntry_Native, EACCES);
#endif
	const auto result = CFileSystemMutator::removeEntry(snapshotOf(base % "/file.bin"));
	REQUIRE(!result.has_value());
	CHECK(result.error().category == FileErrorCategory::PermissionDenied);
	CHECK(!entryAbsent(base % "/file.bin"));
}

//
// Writability
//

TEST_CASE("setEntryWritable and isEntryWritableNoFollow: fresh state round trip", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	const QString filePath = base % "/file.bin";
	writeTestFile(filePath, QByteArray(10, 'f'));

	const auto snapshot = snapshotOf(filePath);

	const auto initiallyWritable = isEntryWritableNoFollow(snapshot);
	REQUIRE(initiallyWritable.has_value());
	CHECK(*initiallyWritable);

	REQUIRE(CFileSystemMutator::setEntryWritable(snapshot, false).has_value());
	const auto afterReadOnly = isEntryWritableNoFollow(snapshot); // Same stale snapshot: the query must answer freshly
	REQUIRE(afterReadOnly.has_value());
	CHECK(!*afterReadOnly);

	REQUIRE(CFileSystemMutator::setEntryWritable(snapshot, true).has_value());
	const auto afterRestore = isEntryWritableNoFollow(snapshot);
	REQUIRE(afterRestore.has_value());
	CHECK(*afterRestore);

	CHECK(readFileContents(filePath) == QByteArray(10, 'f'));
}

#ifdef _WIN32
TEST_CASE("removeEntry: a read-only file fails with PermissionDenied, not ReadOnly", "[mutator]")
{
	// Windows DeleteFile reports ERROR_ACCESS_DENIED for a read-only file - ambiguous evidence, so the
	// classification must stay PermissionDenied; the read-only policy question comes only from the fresh preflight.
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString filePath = tempDir.path() % "/file.bin";
	writeTestFile(filePath, QByteArray(10, 'f'));

	const auto snapshot = snapshotOf(filePath);
	REQUIRE(CFileSystemMutator::setEntryWritable(snapshot, false).has_value());

	const auto result = CFileSystemMutator::removeEntry(snapshot);
	REQUIRE(!result.has_value());
	CHECK(result.error().category == FileErrorCategory::PermissionDenied);

	REQUIRE(CFileSystemMutator::setEntryWritable(snapshot, true).has_value()); // Let the temp dir clean up
}
#else
TEST_CASE("removeEntry: a read-only file is removable on POSIX", "[mutator]")
{
	// POSIX unlink is governed by directory permissions, not the file's own write bits.
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString filePath = tempDir.path() % "/file.bin";
	writeTestFile(filePath, QByteArray(10, 'f'));

	const auto snapshot = snapshotOf(filePath);
	REQUIRE(CFileSystemMutator::setEntryWritable(snapshot, false).has_value());
	REQUIRE(CFileSystemMutator::removeEntry(snapshot).has_value());
	CHECK(entryAbsent(filePath));
}
#endif

TEST_CASE("writability primitives reject link and directory entries and never follow", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

#ifndef _WIN32
	const QString targetPath = base % "/target.bin";
	writeTestFile(targetPath, QByteArray(10, 't'));
	const QString linkPath = base % "/link";
	REQUIRE(QFile::link(targetPath, linkPath));
#else
	REQUIRE(QDir{}.mkpath(base % "/target"));
	const QString linkPath = base % "/dirjunction";
	REQUIRE(createDirectoryLink(base % "/target", linkPath));
#endif

	// A snapshot claiming RegularFile can go stale (or be raced); the primitives must reject the link entry
	// rather than answer for - or remediate - its target.
	const EntrySnapshot staleSnapshot{ ep(linkPath), OperationEntryKind::RegularFile, 0 };

	const auto queryResult = isEntryWritableNoFollow(staleSnapshot);
	REQUIRE(!queryResult.has_value());
	CHECK(queryResult.error().category == FileErrorCategory::Unsupported);

	const auto mutationResult = CFileSystemMutator::setEntryWritable(staleSnapshot, false);
	REQUIRE(!mutationResult.has_value());
	CHECK(mutationResult.error().category == FileErrorCategory::Unsupported);

#ifndef _WIN32
	// The target's own writability was not touched
	const auto targetWritable = isEntryWritableNoFollow(snapshotOf(targetPath));
	REQUIRE(targetWritable.has_value());
	CHECK(*targetWritable);
#endif
}

//
// Directory timestamps
//

TEST_CASE("directory times: read, apply, and follow a directory link on read", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/source"));
	thin_io::entry_times sourceTimes;
	sourceTimes.last_write = thin_io::timestamp{ .seconds = 1'500'000'000, .nanoseconds = 0 };
	if constexpr (thin_io::creation_time_settable)
		sourceTimes.creation = thin_io::timestamp{ .seconds = 1'400'000'000, .nanoseconds = 0 };
	REQUIRE(setEntryTimes(base % "/source", sourceTimes));

	const auto readBack = readCopyableDirectoryTimes(ep(base % "/source"));
	REQUIRE(readBack.has_value());
	CHECK(readBack->lastWrite.seconds == 1'500'000'000);
	if constexpr (thin_io::creation_time_settable)
	{
		REQUIRE(readBack->creation.has_value());
		CHECK(readBack->creation->seconds == 1'400'000'000);
	}
	else
		CHECK(!readBack->creation.has_value()); // Not settable on this platform, so not captured either

	REQUIRE(QDir{}.mkpath(base % "/destination"));
	REQUIRE(CFileSystemMutator::applyDirectoryTimes(ep(base % "/destination"), *readBack).has_value());
	const auto appliedTimes = getEntryTimes(base % "/destination");
	REQUIRE(appliedTimes.has_value());
	REQUIRE(appliedTimes->last_write.has_value());
	CHECK(appliedTimes->last_write->seconds == 1'500'000'000);
	if constexpr (thin_io::creation_time_settable)
	{
		REQUIRE(appliedTimes->creation.has_value());
		CHECK(appliedTimes->creation->seconds == 1'400'000'000);
	}

	// The purpose-specific read follows a directory link to the target it materializes
	REQUIRE(createDirectoryLink(base % "/source", base % "/dirlink"));
	const auto throughLink = readCopyableDirectoryTimes(ep(base % "/dirlink"));
	REQUIRE(throughLink.has_value());
	CHECK(throughLink->lastWrite.seconds == 1'500'000'000);

	const auto missing = readCopyableDirectoryTimes(ep(base % "/no_such_dir"));
	REQUIRE(!missing.has_value());
	CHECK(missing.error().category == FileErrorCategory::NotFound);
}

//
// createDirectories
//

TEST_CASE("createDirectories: creation, deep chain, and pre-existing outcomes", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const auto created = CFileSystemMutator::createDirectories(ep(base % "/newdir"));
	REQUIRE(created.has_value());
	CHECK(*created == DirectoryCreationOutcome::CreatedFinalDirectory);
	CHECK(snapshotOf(base % "/newdir").kind == OperationEntryKind::Directory);

	const auto deep = CFileSystemMutator::createDirectories(ep(base % "/a/b/c/d"));
	REQUIRE(deep.has_value());
	CHECK(*deep == DirectoryCreationOutcome::CreatedFinalDirectory); // Missing parents do not change the outcome
	CHECK(snapshotOf(base % "/a/b/c/d").kind == OperationEntryKind::Directory);

	const auto repeated = CFileSystemMutator::createDirectories(ep(base % "/a/b/c/d"));
	REQUIRE(repeated.has_value());
	CHECK(*repeated == DirectoryCreationOutcome::FinalDirectoryAlreadyExisted);

	// A directory link at the final path counts as the directory existing; resolution decides what that means
	REQUIRE(createDirectoryLink(base % "/newdir", base % "/dirlink"));
	const auto linkExisted = CFileSystemMutator::createDirectories(ep(base % "/dirlink"));
	REQUIRE(linkExisted.has_value());
	CHECK(*linkExisted == DirectoryCreationOutcome::FinalDirectoryAlreadyExisted);
}

TEST_CASE("createDirectories: a non-directory collision and an unusable parent are errors", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/file.bin", QByteArray(10, 'f'));

	const auto fileCollision = CFileSystemMutator::createDirectories(ep(base % "/file.bin"));
	REQUIRE(!fileCollision.has_value());
	CHECK(fileCollision.error().category == FileErrorCategory::AlreadyExists);

	const auto fileParent = CFileSystemMutator::createDirectories(ep(base % "/file.bin/sub"));
	REQUIRE(!fileParent.has_value());
	CHECK(readFileContents(base % "/file.bin") == QByteArray(10, 'f')); // The file was not disturbed
}

TEST_CASE("createDirectories: forced faults at the final creation", "[mutator]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	{
		// A forced collision with nothing actually present: reported as AlreadyExists for resolution to re-inspect.
		CFaultHookScope scope;
#ifdef _WIN32
		scope.forceNativeError(Point::CreateDirectory_FinalNative, ERROR_ALREADY_EXISTS);
#else
		scope.forceNativeError(Point::CreateDirectory_FinalNative, EEXIST);
#endif
		const auto result = CFileSystemMutator::createDirectories(ep(base % "/phantom"));
		REQUIRE(!result.has_value());
		CHECK(result.error().category == FileErrorCategory::AlreadyExists);
		CHECK(entryAbsent(base % "/phantom"));
	}

	{
		CFaultHookScope scope;
#ifdef _WIN32
		scope.forceNativeError(Point::CreateDirectory_FinalNative, ERROR_ACCESS_DENIED);
#else
		scope.forceNativeError(Point::CreateDirectory_FinalNative, EACCES);
#endif
		const auto result = CFileSystemMutator::createDirectories(ep(base % "/denied"));
		REQUIRE(!result.has_value());
		CHECK(result.error().category == FileErrorCategory::PermissionDenied);
	}
}
