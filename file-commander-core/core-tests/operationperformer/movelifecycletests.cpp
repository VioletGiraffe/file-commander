#include "operationperformertesthelpers.h"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <memory>
#include <string>
#include <thread>

TEST_CASE((std::string("Move by copy removes source directories when merging into existing directories #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QString sourceRoot = sourceDirectory.path();
	const QString destinationRoot = targetDirectory.path() % '/' % QFileInfo(sourceRoot).completeBaseName();
	REQUIRE(QDir{}.mkpath(sourceRoot % "/existing/nested"));
	REQUIRE(QDir{}.mkpath(sourceRoot % "/new/empty"));
	REQUIRE(QDir{}.mkpath(destinationRoot % "/existing"));

	writeTestFile(sourceRoot % "/existing/nested/moved.bin", QByteArray(3000, 'A'));
	writeTestFile(sourceRoot % "/new/moved-too.bin", QByteArray(4000, 'B'));
	writeTestFile(destinationRoot % "/existing/retained.bin", QByteArray(500, 'R'));

	auto performer = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(sourceRoot), targetDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*performer, true);
	REQUIRE(runOperationAutoAbortingPrompts(std::move(performer)) == 0);

	REQUIRE(!QFileInfo::exists(sourceRoot));
	REQUIRE(readFileContents(destinationRoot % "/existing/nested/moved.bin") == QByteArray(3000, 'A'));
	REQUIRE(readFileContents(destinationRoot % "/new/moved-too.bin") == QByteArray(4000, 'B'));
	REQUIRE(readFileContents(destinationRoot % "/existing/retained.bin") == QByteArray(500, 'R'));
	REQUIRE(QFileInfo(destinationRoot % "/new/empty").isDir());
}

struct CurrentFileObserver final : public ProgressObserver {
	void onCurrentFileChanged(const QString& /*file*/) override { currentFileReported = true; }

	bool currentFileReported = false;
};

TEST_CASE((std::string("Paused same-drive move performs no rename #") + std::to_string(rand())).c_str(), "[operationperformer-pause]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QString sourcePath = sourceDirectory.path() % "/file.bin";
	const QString destinationPath = targetDirectory.path() % "/file.bin";
	writeTestFile(sourcePath, QByteArray(3000, 'A'));

	auto performer = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(sourcePath), targetDirectory.path());
	auto observer = std::make_unique<CurrentFileObserver>();
	performer->setObserver(observer.get());
	REQUIRE(performer->togglePause());
	performer->start();
	pumpUntil(performer, observer, [&observer] { return observer->currentFileReported; });

	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	REQUIRE(!performer->done());
	REQUIRE(QFileInfo::exists(sourcePath));
	REQUIRE(!QFileInfo::exists(destinationPath));

	performer->cancel();
	REQUIRE_FALSE(performer->togglePause());
	pumpOperationToCompletion(performer, observer);

	REQUIRE(QFileInfo::exists(sourcePath));
	REQUIRE(!QFileInfo::exists(destinationPath));
}

TEST_CASE((std::string("Move cleanup waits while paused #") + std::to_string(rand())).c_str(), "[operationperformer-pause]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QString sourceRoot = sourceDirectory.path();
	const QString destinationRoot = targetDirectory.path() % '/' % QFileInfo(sourceRoot).completeBaseName();
	const QString sourceFilePath = sourceRoot % "/file.bin";
	const QString destinationFilePath = destinationRoot % "/file.bin";
	writeTestFile(sourceFilePath, QByteArray(3000, 'A'));

	auto performer = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(sourceRoot), targetDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*performer, true);
	COperationPerformerTestSeam::setPauseBeforeDirectoryCleanup(*performer, true);
	auto observer = std::make_unique<ProgressObserver>();
	performer->setObserver(observer.get());
	performer->start();
	pumpUntil(performer, observer, [&performer] { return performer->paused(); });

	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	REQUIRE(!performer->done());
	REQUIRE(QFileInfo::exists(sourceRoot));
	REQUIRE(!QFileInfo::exists(sourceFilePath));
	REQUIRE(readFileContents(destinationFilePath) == QByteArray(3000, 'A'));

	REQUIRE_FALSE(performer->togglePause());
	pumpOperationToCompletion(performer, observer);

	REQUIRE(!QFileInfo::exists(sourceRoot));
}
