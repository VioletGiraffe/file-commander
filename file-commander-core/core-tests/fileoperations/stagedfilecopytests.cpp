// WP2: the staged file copy lifecycle - exclusive staging, chunked transfer, required metadata,
// durability policy, atomic publication, and cleanup, with exact FailedAction attribution.

#include "fileoperations/cstagedfilecopy.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#endif

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode accessDeniedCode = ERROR_ACCESS_DENIED;
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
constexpr NativeErrorCode diskFullCode = ERROR_DISK_FULL;
constexpr NativeErrorCode alreadyExistsCode = ERROR_FILE_EXISTS;
constexpr NativeErrorCode unsupportedCode = ERROR_INVALID_FUNCTION;
constexpr NativeErrorCode invalidArgumentCode = ERROR_INVALID_PARAMETER;
#else
constexpr NativeErrorCode accessDeniedCode = EACCES;
constexpr NativeErrorCode ioFailureCode = EIO;
constexpr NativeErrorCode diskFullCode = ENOSPC;
constexpr NativeErrorCode alreadyExistsCode = EEXIST;
constexpr NativeErrorCode unsupportedCode = ENOSYS;
constexpr NativeErrorCode invalidArgumentCode = EINVAL;
#endif

QByteArray patternedContents(const int size)
{
	QByteArray data(size, '\0');
	char* bytes = data.data();
	for (int i = 0; i < size; ++i)
		bytes[i] = static_cast<char>(i * 31 + 7);
	return data;
}

// Drives writeNext() until readyToCommit; returns the number of writeNext() calls made.
int stageAll(CStagedFileCopy& session, const uint64_t chunkSize)
{
	for (int calls = 1; ; ++calls)
	{
		const auto chunk = session.writeNext(chunkSize);
		REQUIRE(chunk.has_value());
		if (chunk->readyToCommit)
			return calls;
	}
}

// A complete session over an already-validated begin; returns commit()'s result.
std::expected<void, FailureDetails> copyFile(const QString& source, const QString& destination,
	const ReplacementMode replacement = ReplacementMode::RequireAbsent,
	const CommitDurability durability = CommitDurability::NoFlush,
	const uint64_t chunkSize = 64 * 1024)
{
	auto session = CStagedFileCopy::begin(ep(source), ep(destination));
	REQUIRE(session.has_value());
	stageAll(*session, chunkSize);
	return session->commit(replacement, durability);
}

int stagingFileCount(const QString& directory)
{
	return static_cast<int>(QDir{ directory }.entryList({ QStringLiteral(".file-commander-copy-*") },
		QDir::Files | QDir::Hidden | QDir::System).size());
}

#ifdef _WIN32
DWORD entryAttributes(const QString& path)
{
	const DWORD attributes = ::GetFileAttributesW(path.toStdWString().c_str());
	REQUIRE(attributes != INVALID_FILE_ATTRIBUTES);
	return attributes;
}

void setEntryAttributes(const QString& path, const DWORD attributes)
{
	REQUIRE(::SetFileAttributesW(path.toStdWString().c_str(), attributes) != 0);
}
#else
uint32_t entryPermissionMode(const QString& path)
{
	struct stat entryStat;
	REQUIRE(::stat(QFile::encodeName(path).constData(), &entryStat) == 0);
	return static_cast<uint32_t>(entryStat.st_mode) & 07777u;
}
#endif

} // namespace

//
// The successful lifecycle
//

TEST_CASE("staged copy: empty, one-chunk, and multi-chunk files", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("empty file")
	{
		writeTestFile(base % "/empty.bin", {});
		REQUIRE(copyFile(base % "/empty.bin", base % "/empty-copy.bin").has_value());
		CHECK(readFileContents(base % "/empty-copy.bin").isEmpty());
		CHECK(snapshotOf(base % "/empty-copy.bin").kind == OperationEntryKind::RegularFile);
	}

	SECTION("one chunk")
	{
		const QByteArray contents = patternedContents(3000);
		writeTestFile(base % "/small.bin", contents);
		REQUIRE(copyFile(base % "/small.bin", base % "/small-copy.bin").has_value());
		CHECK(readFileContents(base % "/small-copy.bin") == contents);
	}

	SECTION("multiple chunks")
	{
		const QByteArray contents = patternedContents(300'000);
		writeTestFile(base % "/large.bin", contents);

		auto session = CStagedFileCopy::begin(ep(base % "/large.bin"), ep(base % "/large-copy.bin"));
		REQUIRE(session.has_value());
		CHECK(stageAll(*session, 64 * 1024) > 1);
		REQUIRE(session->commit(ReplacementMode::RequireAbsent, CommitDurability::NoFlush).has_value());
		CHECK(readFileContents(base % "/large-copy.bin") == contents);
	}

	CHECK(stagingFileCount(base) == 0);
}

TEST_CASE("staged copy: the destination entry appears only at publication", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", patternedContents(200'000));

	auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
	REQUIRE(session.has_value());
	CHECK(stagingFileCount(base) == 1);

	const auto firstChunk = session->writeNext(64 * 1024);
	REQUIRE(firstChunk.has_value());
	CHECK(!firstChunk->readyToCommit);
	CHECK(entryAbsent(base % "/dest.bin"));

	stageAll(*session, 64 * 1024);
	CHECK(entryAbsent(base % "/dest.bin")); // Fully staged, still unpublished

	REQUIRE(session->commit(ReplacementMode::RequireAbsent, CommitDurability::NoFlush).has_value());
	CHECK(snapshotOf(base % "/dest.bin").kind == OperationEntryKind::RegularFile);
	CHECK(stagingFileCount(base) == 0);
}

TEST_CASE("staged copy: unique staging creation retries on a name collision", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", patternedContents(1000));

	CFaultHookScope scope;
	scope.forceNativeError(Point::StagedCopy_CreateStaging_Native, alreadyExistsCode);

	REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin").has_value());
	CHECK(scope.forcedErrorConsumed(Point::StagedCopy_CreateStaging_Native));
	CHECK(scope.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 2); // The collision, then the successful retry
	CHECK(stagingFileCount(base) == 0);
}

//
// Failure exits and old-destination preservation
//

TEST_CASE("staged copy: the old destination is preserved through every pre-publication failure", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray oldContents{ "OLD DESTINATION" };
	writeTestFile(base % "/dest.bin", oldContents);
	writeTestFile(base % "/source.bin", patternedContents(10'000));

	SECTION("staging creation failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_CreateStaging_Native, accessDeniedCode);

		const auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::PrepareStagingFile);
		CHECK(session.error().filesystemError.category == FileErrorCategory::PermissionDenied);
		CHECK(session.error().filesystemError.nativeCode == accessDeniedCode);
	}

	SECTION("resize failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_ResizeStaging_Native, diskFullCode);

		const auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::PrepareStagingFile);
		CHECK(session.error().filesystemError.category == FileErrorCategory::NotEnoughSpace);
	}

	SECTION("write failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_WriteStaging_Native, ioFailureCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		const auto chunk = session->writeNext(64 * 1024);
		REQUIRE(!chunk.has_value());
		CHECK(chunk.error().action == FailedAction::WriteDestination);
		CHECK(chunk.error().filesystemError.category == FileErrorCategory::IoFailure);
		REQUIRE(session->abort().has_value());
	}

	SECTION("flush failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_FlushStaging_Native, ioFailureCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		stageAll(*session, 64 * 1024);
		const auto committed = session->commit(ReplacementMode::ReplaceExistingFile, CommitDurability::FlushBeforePublish);
		REQUIRE(!committed.has_value());
		CHECK(committed.error().action == FailedAction::WriteDestination);
		REQUIRE(session->abort().has_value());
	}

	SECTION("metadata application failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_ApplyMetadata_Native, accessDeniedCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		stageAll(*session, 64 * 1024);
		const auto committed = session->commit(ReplacementMode::ReplaceExistingFile, CommitDurability::NoFlush);
		REQUIRE(!committed.has_value());
		CHECK(committed.error().action == FailedAction::PreserveFileMetadata);
		CHECK(committed.error().filesystemError.category == FileErrorCategory::PermissionDenied);
		REQUIRE(session->abort().has_value());
	}

	SECTION("pre-publication close failure")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_CloseStaging_Native, ioFailureCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		stageAll(*session, 64 * 1024);
		const auto committed = session->commit(ReplacementMode::ReplaceExistingFile, CommitDurability::NoFlush);
		REQUIRE(!committed.has_value());
		CHECK(committed.error().action == FailedAction::WriteDestination);
		REQUIRE(session->abort().has_value());
	}

	SECTION("publication failure")
	{
		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		stageAll(*session, 64 * 1024);
		const auto committed = session->commit(ReplacementMode::RequireAbsent, CommitDurability::NoFlush);
		REQUIRE(!committed.has_value());
		CHECK(committed.error().action == FailedAction::PublishDestination);
		CHECK(committed.error().filesystemError.category == FileErrorCategory::AlreadyExists);
		REQUIRE(session->abort().has_value());
	}

	CHECK(readFileContents(base % "/dest.bin") == oldContents);
	CHECK(stagingFileCount(base) == 0);
}

TEST_CASE("staged copy: begin failures leave nothing behind", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("absent source")
	{
		const auto session = CStagedFileCopy::begin(ep(base % "/no-such-file.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::ReadSource);
		CHECK(session.error().filesystemError.category == FileErrorCategory::NotFound);
	}

	SECTION("metadata capture failure")
	{
		writeTestFile(base % "/source.bin", patternedContents(1000));

		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_CaptureMetadata_Native, accessDeniedCode);

		const auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::PreserveFileMetadata);
		CHECK(session.error().filesystemError.category == FileErrorCategory::PermissionDenied);
	}

	CHECK(entryAbsent(base % "/dest.bin"));
	CHECK(stagingFileCount(base) == 0);
}

TEST_CASE("staged copy: late destination appearance vs authorized replacement", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray newContents = patternedContents(5000);
	writeTestFile(base % "/source.bin", newContents);

	auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
	REQUIRE(session.has_value());
	stageAll(*session, 64 * 1024);

	// The destination appears after staging is complete but before publication
	const QByteArray sneakedContents{ "SNEAKED IN" };
	writeTestFile(base % "/dest.bin", sneakedContents);

	SECTION("require-absent publication refuses")
	{
		const auto committed = session->commit(ReplacementMode::RequireAbsent, CommitDurability::NoFlush);
		REQUIRE(!committed.has_value());
		CHECK(committed.error().action == FailedAction::PublishDestination);
		CHECK(committed.error().filesystemError.category == FileErrorCategory::AlreadyExists);
		REQUIRE(session->abort().has_value());
		CHECK(readFileContents(base % "/dest.bin") == sneakedContents);
	}

	SECTION("authorized replacement replaces atomically")
	{
		REQUIRE(session->commit(ReplacementMode::ReplaceExistingFile, CommitDurability::NoFlush).has_value());
		CHECK(readFileContents(base % "/dest.bin") == newContents);
	}

	CHECK(stagingFileCount(base) == 0);
}

//
// Required metadata
//

TEST_CASE("staged copy: transfers permissions and every settable timestamp", "[stagedcopy][metadata]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(2000);
	writeTestFile(base % "/source.bin", contents);
	REQUIRE(setEntryTimes(base % "/source.bin",
		{ .creation = thin_io::timestamp{ .seconds = 1'400'000'000 }, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = 1'600'000'000 } }));
#ifndef _WIN32
	REQUIRE(::chmod(QFile::encodeName(base % "/source.bin").constData(), 0754) == 0);
#endif

	REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin").has_value());
	CHECK(readFileContents(base % "/dest.bin") == contents);

	const auto destinationTimes = getEntryTimes(base % "/dest.bin");
	REQUIRE(destinationTimes.has_value());
	REQUIRE(destinationTimes->last_write.has_value());
	CHECK(destinationTimes->last_write->seconds == 1'600'000'000);
	if constexpr (thin_io::creation_time_settable)
	{
		REQUIRE(destinationTimes->creation.has_value());
		CHECK(destinationTimes->creation->seconds == 1'400'000'000);
	}

#ifndef _WIN32
	CHECK(entryPermissionMode(base % "/dest.bin") == 0754u);
#endif
}

#ifdef _WIN32
TEST_CASE("staged copy: Windows attribute fidelity", "[stagedcopy][metadata]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("hidden, system, and read-only transfer")
	{
		writeTestFile(base % "/special.bin", patternedContents(100));
		setEntryAttributes(base % "/special.bin", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

		REQUIRE(copyFile(base % "/special.bin", base % "/special-copy.bin").has_value());

		const DWORD copyAttributes = entryAttributes(base % "/special-copy.bin");
		CHECK((copyAttributes & FILE_ATTRIBUTE_HIDDEN) != 0);
		CHECK((copyAttributes & FILE_ATTRIBUTE_SYSTEM) != 0);
		CHECK((copyAttributes & FILE_ATTRIBUTE_READONLY) != 0);

		// Restore writability so the temporary directory can be cleaned up
		setEntryAttributes(base % "/special.bin", FILE_ATTRIBUTE_NORMAL);
		setEntryAttributes(base % "/special-copy.bin", FILE_ATTRIBUTE_NORMAL);
	}

	SECTION("a plain source yields a plain visible file despite the hidden staging")
	{
		writeTestFile(base % "/plain.bin", patternedContents(100));

		REQUIRE(copyFile(base % "/plain.bin", base % "/plain-copy.bin").has_value());

		const DWORD copyAttributes = entryAttributes(base % "/plain-copy.bin");
		CHECK((copyAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) == 0);
	}
}
#endif

#ifndef _WIN32
TEST_CASE("staged copy: a file-link source materializes the followed target with the target's metadata", "[stagedcopy][metadata][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("healthy link")
	{
		const QByteArray targetContents = patternedContents(4000);
		writeTestFile(base % "/target.bin", targetContents);
		REQUIRE(::chmod(QFile::encodeName(base % "/target.bin").constData(), 0640) == 0);
		REQUIRE(setEntryTimes(base % "/target.bin", { .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = 1'500'000'000 } }));
		REQUIRE(QFile::link(base % "/target.bin", base % "/link.bin"));

		REQUIRE(copyFile(base % "/link.bin", base % "/dest.bin").has_value());

		CHECK(snapshotOf(base % "/dest.bin").kind == OperationEntryKind::RegularFile); // Materialized, not a link copy
		CHECK(readFileContents(base % "/dest.bin") == targetContents);
		CHECK(entryPermissionMode(base % "/dest.bin") == 0640u);
		const auto destinationTimes = getEntryTimes(base % "/dest.bin");
		REQUIRE(destinationTimes.has_value());
		CHECK(destinationTimes->last_write->seconds == 1'500'000'000);

		// The link and its target are untouched
		CHECK(snapshotOf(base % "/link.bin").kind == OperationEntryKind::FileLink);
		CHECK(readFileContents(base % "/target.bin") == targetContents);
		CHECK(entryPermissionMode(base % "/target.bin") == 0640u);
	}

	SECTION("broken link fails as an ordinary source-read failure")
	{
		REQUIRE(QFile::link(base % "/gone.bin", base % "/broken-link.bin"));

		const auto session = CStagedFileCopy::begin(ep(base % "/broken-link.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::ReadSource);
		CHECK(session.error().filesystemError.category == FileErrorCategory::NotFound);
		CHECK(entryAbsent(base % "/dest.bin"));
	}

	CHECK(stagingFileCount(base) == 0);
}
#endif

//
// Destination link and alias entries
//

TEST_CASE("staged copy: replacing a hard-link alias replaces only that pathname", "[stagedcopy][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray aliasContents{ "ALIASED DATA" };
	writeTestFile(base % "/original.bin", aliasContents);
	REQUIRE(createHardLink(base % "/original.bin", base % "/alias.bin"));

	const QByteArray newContents = patternedContents(3000);
	writeTestFile(base % "/source.bin", newContents);

	REQUIRE(copyFile(base % "/source.bin", base % "/alias.bin", ReplacementMode::ReplaceExistingFile).has_value());

	CHECK(readFileContents(base % "/alias.bin") == newContents);
	CHECK(readFileContents(base % "/original.bin") == aliasContents); // The other alias still holds the old data
}

#ifndef _WIN32
TEST_CASE("staged copy: replacing a symlink destination replaces the link entry, not the target", "[stagedcopy][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray targetContents{ "LINK TARGET" };
	writeTestFile(base % "/target.bin", targetContents);
	REQUIRE(QFile::link(base % "/target.bin", base % "/dest-link.bin"));

	const QByteArray newContents = patternedContents(2000);
	writeTestFile(base % "/source.bin", newContents);

	REQUIRE(copyFile(base % "/source.bin", base % "/dest-link.bin", ReplacementMode::ReplaceExistingFile).has_value());

	CHECK(snapshotOf(base % "/dest-link.bin").kind == OperationEntryKind::RegularFile); // The link entry is gone
	CHECK(readFileContents(base % "/dest-link.bin") == newContents);
	CHECK(readFileContents(base % "/target.bin") == targetContents);
}
#endif

//
// Preallocation and durability policies
//

TEST_CASE("staged copy: unsupported preallocation is best-effort, real exhaustion is fatal", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(5000);
	writeTestFile(base % "/source.bin", contents);

	SECTION("filesystem cannot service the request - invalid-argument form")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_PreallocateStaging_Native, invalidArgumentCode);

		REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin").has_value());
		CHECK(readFileContents(base % "/dest.bin") == contents);
	}

	SECTION("filesystem cannot service the request - unsupported-call form")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_PreallocateStaging_Native, unsupportedCode);

		REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin").has_value());
		CHECK(readFileContents(base % "/dest.bin") == contents);
	}

	SECTION("storage exhaustion at preallocation is fatal")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_PreallocateStaging_Native, diskFullCode);

		const auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(!session.has_value());
		CHECK(session.error().action == FailedAction::PrepareStagingFile);
		CHECK(session.error().filesystemError.category == FileErrorCategory::NotEnoughSpace);
		CHECK(stagingFileCount(base) == 0);
	}

	SECTION("storage exhaustion during the transfer is fatal")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_WriteStaging_Native, diskFullCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		const auto chunk = session->writeNext(64 * 1024);
		REQUIRE(!chunk.has_value());
		CHECK(chunk.error().action == FailedAction::WriteDestination);
		CHECK(chunk.error().filesystemError.category == FileErrorCategory::NotEnoughSpace);
		REQUIRE(session->abort().has_value());
		CHECK(stagingFileCount(base) == 0);
	}
}

TEST_CASE("staged copy: the data flush runs exactly when the durability policy demands it", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", patternedContents(1000));

	SECTION("fresh copy does not flush")
	{
		CFaultHookScope scope;
		REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin", ReplacementMode::RequireAbsent, CommitDurability::NoFlush).has_value());
		CHECK(scope.arrivalCount(Point::StagedCopy_FlushStaging_Native) == 0);
	}

	SECTION("move/replacement durability flushes before publication")
	{
		CFaultHookScope scope;
		REQUIRE(copyFile(base % "/source.bin", base % "/dest.bin", ReplacementMode::RequireAbsent, CommitDurability::FlushBeforePublish).has_value());
		CHECK(scope.arrivalCount(Point::StagedCopy_FlushStaging_Native) == 1);
	}
}

//
// Abort and destructor cleanup
//

TEST_CASE("staged copy: explicit abort removes staging and touches nothing else", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(100'000);
	writeTestFile(base % "/source.bin", contents);

	SECTION("mid-transfer abort")
	{
		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		REQUIRE(session->writeNext(64 * 1024).has_value());

		REQUIRE(session->abort().has_value());
		CHECK(stagingFileCount(base) == 0);
		CHECK(entryAbsent(base % "/dest.bin"));
		CHECK(readFileContents(base % "/source.bin") == contents);
	}

	SECTION("cleanup failure is reported as CleanupStaging")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_RemoveStaging_Native, ioFailureCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());

		const auto aborted = session->abort();
		REQUIRE(!aborted.has_value());
		CHECK(aborted.error().action == FailedAction::CleanupStaging);
		CHECK(aborted.error().filesystemError.category == FileErrorCategory::IoFailure);

		// The report is truthful: the staging file really is still there; clean it up manually
		CHECK(stagingFileCount(base) == 1);
		for (const QString& leftover : QDir{ base }.entryList({ QStringLiteral(".file-commander-copy-*") }, QDir::Files | QDir::Hidden | QDir::System))
			QFile::remove(base % '/' % leftover);
	}

	SECTION("a permission-denied removal is remediated by restoring writability")
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::StagedCopy_RemoveStaging_Native, accessDeniedCode);

		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());

		REQUIRE(session->abort().has_value()); // The retry after the writability fix succeeds
		CHECK(stagingFileCount(base) == 0);
	}
}

TEST_CASE("staged copy: abort after a failed publication of a read-only source's copy", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// The staging file inherits the source's read-only permission during commit; when publication then
	// fails, cleanup must still be able to remove it.
	writeTestFile(base % "/source.bin", patternedContents(1000));
	writeTestFile(base % "/dest.bin", QByteArray{ "OCCUPIED" });
#ifdef _WIN32
	setEntryAttributes(base % "/source.bin", FILE_ATTRIBUTE_READONLY);
#else
	REQUIRE(::chmod(QFile::encodeName(base % "/source.bin").constData(), 0444) == 0);
#endif

	auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
	REQUIRE(session.has_value());
	stageAll(*session, 64 * 1024);
	const auto committed = session->commit(ReplacementMode::RequireAbsent, CommitDurability::NoFlush);
	REQUIRE(!committed.has_value());
	CHECK(committed.error().action == FailedAction::PublishDestination);

	REQUIRE(session->abort().has_value());
	CHECK(stagingFileCount(base) == 0);
	CHECK(readFileContents(base % "/dest.bin") == QByteArray{ "OCCUPIED" });

	// Restore writability so the temporary directory can be cleaned up
#ifdef _WIN32
	setEntryAttributes(base % "/source.bin", FILE_ATTRIBUTE_NORMAL);
#else
	REQUIRE(::chmod(QFile::encodeName(base % "/source.bin").constData(), 0644) == 0);
#endif
}

TEST_CASE("staged copy: the destructor cleans up an unfinished session", "[stagedcopy]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", patternedContents(100'000));

	CFaultHookScope scope;
	{
		auto session = CStagedFileCopy::begin(ep(base % "/source.bin"), ep(base % "/dest.bin"));
		REQUIRE(session.has_value());
		REQUIRE(session->writeNext(64 * 1024).has_value());
		CHECK(stagingFileCount(base) == 1);
	}

	CHECK(scope.arrivalCount(Point::StagedCopy_RemoveStaging_Native) == 1); // The destructor's fallback cleanup ran
	CHECK(stagingFileCount(base) == 0);
	CHECK(entryAbsent(base % "/dest.bin"));
}
