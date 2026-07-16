#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"

#include "fs.hpp"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDir>
#include <QStringBuilder>
#include <QStringList>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <string>

struct CFileManipulatorTestSeam {
	static void invalidateSourceFileTime(CFileManipulator& manipulator, const QFileDevice::FileTime fileTimeType) {
		manipulator._sourceFileTime[fileTimeType] = {};
	}
};

TEST_CASE((std::string("Timestamp-transfer failure does not publish the staging file #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QByteArray sourceContents(2048, 'A');
	const QByteArray originalDestinationContents(500, 'B');
	const QString sourcePath = sourceDirectory.path() % "/file.bin";
	const QString destinationPath = targetDirectory.path() % "/file.bin";
	writeTestFile(sourcePath, sourceContents);
	writeTestFile(destinationPath, originalDestinationContents);

	const CFileSystemObject sourceObject(sourcePath);
	CFileManipulator manipulator(sourceObject);
	REQUIRE(manipulator.copyChunk(1024, targetDirectory.path() % '/') == FileOperationResultCode::Ok);
	REQUIRE(manipulator.copyOperationInProgress());

	CFileManipulatorTestSeam::invalidateSourceFileTime(manipulator, QFileDevice::FileModificationTime);
	REQUIRE(manipulator.copyChunk(1024, targetDirectory.path() % '/') == FileOperationResultCode::Fail);
	REQUIRE_FALSE(manipulator.copyOperationInProgress());
	CHECK(manipulator.lastErrorMessage().contains(QStringLiteral("modification time"), Qt::CaseInsensitive));
	REQUIRE(manipulator.cancelCopy() == FileOperationResultCode::Ok);

	REQUIRE(readFileContents(sourcePath) == sourceContents);
	REQUIRE(readFileContents(destinationPath) == originalDestinationContents);
	CHECK(QDir(targetDirectory.path()).entryList(QStringList{ QStringLiteral(".file-commander-copy-*.tmp") }, QDir::Files | QDir::Hidden | QDir::System).isEmpty());
}

// Leaves the access time unrequested - see checkTimesMatch()
static thin_io::entry_times creationAndWriteTimes(const int64_t creationSeconds, const int64_t lastWriteSeconds)
{
	thin_io::entry_times times;
	if constexpr (thin_io::creation_time_settable)
		times.creation = thin_io::timestamp{ .seconds = creationSeconds };

	times.last_write = thin_io::timestamp{ .seconds = lastWriteSeconds };
	return times;
}

// Every path handed to these is built in this file and never carries the trailing slash that CFileSystemObject
// normalizes onto a directory path, so none of them needs stripping first.
static void setTimes(const QString& path, const thin_io::entry_times& times)
{
	REQUIRE(thin_io::set_times(path.toUtf8().constData(), times));
}

[[nodiscard]] static thin_io::entry_times getTimes(const QString& path)
{
	const auto times = thin_io::get_times(path.toUtf8().constData());
	REQUIRE(times);
	return *times;
}

// The access time is deliberately not compared: enumerating the source folder updates that folder's own access time
// before the operation gets to read it, so what reaches the destination is whatever the scan left behind - and whether
// it moves at all comes down to the platform and the mount options.
static void checkTimesMatch(const thin_io::entry_times& actual, const thin_io::entry_times& expected)
{
	CHECK(actual.last_write == expected.last_write);
	if constexpr (thin_io::creation_time_settable)
		CHECK(actual.creation == expected.creation);
}

struct DirectoryTimestampSetup {
	DirectoryTimestampSetup() :
		sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX"),
		targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX"),
		sourceRoot(sourceDirectory.path()),
		destinationRoot(targetDirectory.path() % '/' % CFileSystemObject(sourceRoot).fullName())
	{
		REQUIRE(sourceDirectory.isValid());
		REQUIRE(targetDirectory.isValid());
		REQUIRE(QDir{}.mkpath(sourceRoot % "/nested"));
		writeTestFile(sourceRoot % "/root.bin", rootContents);
		writeTestFile(sourceRoot % "/nested/nested.bin", nestedContents);

		// Stamped only once the contents exist, because creating an entry inside a folder updates that folder's own
		// modification time - the very reason the operation defers its stamping to the end
		setTimes(sourceRoot, rootTimes);
		setTimes(sourceRoot % "/nested", nestedTimes);
	}

	QTemporaryDir sourceDirectory;
	QTemporaryDir targetDirectory;
	const QString sourceRoot;
	const QString destinationRoot;
	const QByteArray rootContents = QByteArray(2000, 'R');
	const QByteArray nestedContents = QByteArray(3000, 'N');
	// Distinct per folder, so stamping every folder from one source is a failure rather than a coincidence, and far
	// enough in the past that a folder left with the time of the copy instead is unmistakable
	const thin_io::entry_times rootTimes = creationAndWriteTimes(1'100'000'000, 1'400'000'000);
	const thin_io::entry_times nestedTimes = creationAndWriteTimes(1'000'000'000, 1'300'000'000);
};

static void checkCopiedDirectoriesKeepSourceTimes(const Operation operation, const bool forceMoveByCopy)
{
	DirectoryTimestampSetup setup;

	auto performer = std::make_unique<COperationPerformer>(operation, CFileSystemObject(setup.sourceRoot), setup.targetDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*performer, forceMoveByCopy);
	REQUIRE(runOperationAutoAbortingPrompts(std::move(performer)) == 0);

	REQUIRE(readFileContents(setup.destinationRoot % "/nested/nested.bin") == setup.nestedContents);
	checkTimesMatch(getTimes(setup.destinationRoot), setup.rootTimes);
	checkTimesMatch(getTimes(setup.destinationRoot % "/nested"), setup.nestedTimes);
}

TEST_CASE((std::string("Copying a directory tree transfers the source folders' timestamps #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	checkCopiedDirectoriesKeepSourceTimes(operationCopy, false);
}

TEST_CASE((std::string("Moving a directory tree by copy transfers the source folders' timestamps #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	checkCopiedDirectoriesKeepSourceTimes(operationMove, true);
}

TEST_CASE((std::string("Merging into an existing folder leaves that folder's timestamps alone #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	DirectoryTimestampSetup setup;

	// The destination root exists, so the copy merges into it; "nested" does not, so the copy creates it
	REQUIRE(QDir{}.mkpath(setup.destinationRoot));
	const thin_io::entry_times preExistingTimes = creationAndWriteTimes(900'000'000, 1'200'000'000);
	setTimes(setup.destinationRoot, preExistingTimes);

	auto performer = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(setup.sourceRoot), setup.targetDirectory.path());
	REQUIRE(runOperationAutoAbortingPrompts(std::move(performer)) == 0);

	REQUIRE(readFileContents(setup.destinationRoot % "/root.bin") == setup.rootContents);

	// root.bin landing inside moves the modification time to now regardless, but the source's timestamps must not have
	// been applied to a folder this operation did not create
	const thin_io::entry_times merged = getTimes(setup.destinationRoot);
	CHECK(merged.last_write != setup.rootTimes.last_write);
	if constexpr (thin_io::creation_time_settable)
		CHECK(merged.creation == preExistingTimes.creation);

	// The folder the copy did create is stamped as usual
	checkTimesMatch(getTimes(setup.destinationRoot % "/nested"), setup.nestedTimes);
}
