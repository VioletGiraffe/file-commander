#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"

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
