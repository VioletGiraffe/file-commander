// Cross-volume transfers driven by a genuine native cross-device error rather than a forced one: the only
// coverage that validates the premise every other cross-device test assumes - that a real rename between
// volumes reports ERROR_NOT_SAME_DEVICE / EXDEV, and so actually engages the staged-copy fallback.
// Opt-in: needs a writable directory on a volume other than the one holding TEMP.

#include "fileoperations/ctransferexecutor.h"
#include "fileoperations/coperationexecutioncontext.h"
#include "fileoperations/operationtesthooks.h"

#include "cfilesystemobject.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

constexpr const char* secondVolumeVariable = "FILE_COMMANDER_TEST_SECOND_VOLUME";

OperationSummary runTransfer(OperationScript& script, const TransferKind kind, const QStringList& sources,
	const DestinationIntent intent, const QString& destination)
{
	const auto request = makeTransferRequest(kind, sources, intent, destination);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, 64 * 1024 };
	return executor.run(*request);
}

} // namespace

TEST_CASE("cross-volume transfer: copy then move against a real second volume", "[crossvolume]")
{
	if (!qEnvironmentVariableIsSet(secondVolumeVariable))
	{
		WARN("FILE_COMMANDER_TEST_SECOND_VOLUME is unset: real cross-volume coverage skipped");
		return;
	}

	QTemporaryDir sourceDir; // Under TEMP - the first volume
	REQUIRE(sourceDir.isValid());
	QTemporaryDir destinationDir{ qEnvironmentVariable(secondVolumeVariable) % QStringLiteral("/fc-crossvolume-XXXXXX") };
	REQUIRE(destinationDir.isValid());

	const QString source = sourceDir.path();
	const QString destination = destinationDir.path();

	// Fail loudly rather than skip: a same-volume value here means CI is misconfigured and every assertion
	// below would pass through the rename path, proving nothing.
	REQUIRE(CFileSystemObject{ source }.rootFileSystemId() != CFileSystemObject{ destination }.rootFileSystemId());

	REQUIRE(QDir{}.mkpath(source % "/tree/sub"));
	writeTestFile(source % "/tree/a.bin", patternedContents(4000));
	writeTestFile(source % "/tree/sub/b.bin", patternedContents(6000));
	REQUIRE(QDir{}.mkpath(destination % "/copied"));
	REQUIRE(QDir{}.mkpath(destination % "/moved"));

	OperationScript script;

	// Copy leaves the source intact, so the move below consumes the same tree - one fixture, both operations.
	{
		const int64_t sourceWriteTime = entryLastWriteSeconds(source % "/tree/a.bin");
		const auto summary = runTransfer(script, TransferKind::Copy, { source % "/tree" }, DestinationIntent::IntoDirectory,
			destination % "/copied");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.transferredBytes == 10000);
		CHECK(script.seenRequests.empty());

		// Also the staging-leftover check: its entry filter includes hidden and system files.
		requireEqualTrees(source % "/tree", destination % "/copied/tree");
		CHECK(entryLastWriteSeconds(destination % "/copied/tree/a.bin") == sourceWriteTime);
	}

	{
		CFaultHookScope hooks;
		const auto summary = runTransfer(script, TransferKind::Move, { source % "/tree" }, DestinationIntent::IntoDirectory,
			destination % "/moved");

		CHECK(summary.status == CompletionStatus::Completed);
		// A same-volume move renames the root and transfers nothing; these two assertions are what prove the
		// native rename really reported cross-device and the fallback took over.
		CHECK(summary.transferredBytes == 10000);
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 2);
		CHECK(script.seenRequests.empty());

		CHECK(entryAbsent(source % "/tree"));
		CHECK(readFileContents(destination % "/moved/tree/a.bin") == patternedContents(4000));
		CHECK(readFileContents(destination % "/moved/tree/sub/b.bin") == patternedContents(6000));
		CHECK(stagingFileCount(destination % "/moved/tree") == 0);
		CHECK(stagingFileCount(destination % "/moved/tree/sub") == 0);
	}
}
