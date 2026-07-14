#define CATCH_CONFIG_RUNNER

#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"
#include "cfolderenumeratorrecursive.h"
#include "ctestfoldergenerator.h"
#include "system/ctimeelapsed.h"
#include "filecomparator/cfilecomparator.h"

// test_utils
#include "foldercomparator.h"
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

#include "lang/type_traits_fast.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDir>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

//#include "3rdparty/catch2/catch_template_test_macros.hpp"

static uint32_t g_randomSeed = []{
	std::random_device rd;
	return std::uniform_int_distribution<uint32_t>{0, uint32_max}(rd);
}();

static constexpr QFileDevice::FileTime supportedFileTimeTypes[] {
	QFileDevice::FileAccessTime,
#ifndef __linux__
	QFileDevice::FileBirthTime,
#endif
#if !defined __linux__ && !defined _WIN32
	QFileDevice::FileMetadataChangeTime,
#endif
	QFileDevice::FileModificationTime,
};

static bool timesAlmostMatch(const QDateTime& t1, const QDateTime& t2, const QFileDevice::FileTime type)
{
	const qint64 diff = ::abs(t1.toMSecsSinceEpoch() - t2.toMSecsSinceEpoch());

	qint64 allowedTimeDiffMs = 0;
	switch (type)
	{
	// These margins are generally too long, but necessary for CI systems (sloooow cloud virtualized hardware)
	case QFileDevice::FileAccessTime:
		allowedTimeDiffMs = 3000;
		break;
	case QFileDevice::FileBirthTime:
		allowedTimeDiffMs = 100;
		break;
	case QFileDevice::FileMetadataChangeTime:
		allowedTimeDiffMs = 2500;
		break;
	case QFileDevice::FileModificationTime:
		allowedTimeDiffMs = 100;
		break;
	default:
		assert(!"Unknown QFileDevice::FileTime");
		return false;
	}

#ifdef __APPLE__
	qint64 multiplier = 50;
#else
	qint64 multiplier = 1;
#endif

#ifdef _DEBUG
	multiplier *= 2;
#endif

	if (diff <= allowedTimeDiffMs * multiplier)
		return true;
	else
	{
		std::cerr << "Time mismatch for type " << type << ": diff = " << diff << ", allowed = " << allowedTimeDiffMs * multiplier << '\n';
		return false;
	}
}

static bool createHardLink(const QString& existingPath, const QString& linkPath)
{
#ifdef _WIN32
	return CreateHardLinkW(reinterpret_cast<const WCHAR*>(linkPath.utf16()), reinterpret_cast<const WCHAR*>(existingPath.utf16()), nullptr) != 0;
#else
	return ::link(QFile::encodeName(existingPath).constData(), QFile::encodeName(linkPath).constData()) == 0;
#endif
}

static void copyTest(const size_t maxFileSize)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	REQUIRE(QFileInfo::exists(targetDirectory.path()));
	REQUIRE(CFileSystemObject(targetDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, maxFileSize));

	ProgressObserver progressObserver;
	COperationPerformer p(operationCopy, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	CTimeElapsed timer(true);

	p.setObserver(&progressObserver);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(sourceDirectory.path(), sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(targetDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName(), destTree);

	CFileComparator comparator;
	REQUIRE(sourceTree.size() == destTree.size());

	for (size_t i = 0, n = sourceTree.size(); i < n; ++i)
	{
		const auto& sourceItem = sourceTree[i];
		const auto& destItem = destTree[i];
		if (!sourceItem.isFile())
			continue;

		QFile fileA(sourceItem.fullAbsolutePath());
		REQUIRE(fileA.open(QFile::ReadOnly));

		QFile fileB(destItem.fullAbsolutePath());
		REQUIRE(fileB.open(QFile::ReadOnly));

		comparator.compareFiles(fileA, fileB, [](int) {}, [&fileA, &fileB](const CFileComparator::ComparisonResult result) {
			if (result != CFileComparator::Equal) [[unlikely]]
			{
				std::string msg;
				if (fileA.size() != fileB.size())
					msg = "Files are not equal (sizes differ: " + std::to_string(fileA.size()) + " and " + std::to_string(fileB.size()) + "): ";
				else
					msg = "Files are not equal: ";

				msg += fileA.fileName().toStdString() + " and " + fileB.fileName().toStdString();
				FAIL_CHECK(msg);
			}
			else
				CHECK(true);
			});

		for (const auto fileTimeType : supportedFileTimeTypes)
		{
			if (!timesAlmostMatch(fileA.fileTime(fileTimeType), fileB.fileTime(fileTimeType), fileTimeType))
				FAIL_CHECK();
		}
	}

	REQUIRE(compareFolderContents(sourceTree, destTree));
}

static void moveTest(const size_t maxFileSize)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	REQUIRE(QFileInfo::exists(targetDirectory.path()));
	REQUIRE(CFileSystemObject(targetDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, maxFileSize));

	std::vector<CFileSystemObject> sourceTree;
	CFolderEnumeratorRecursive::enumerateFolder(sourceDirectory.path(), sourceTree);

	ProgressObserver progressObserver;
	COperationPerformer p(operationMove, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	p.setObserver(&progressObserver);

	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	REQUIRE(!CFileSystemObject(sourceDirectory.path()).exists());

	std::vector<CFileSystemObject> destTree;
	CFolderEnumeratorRecursive::enumerateFolder(targetDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName(), destTree);

	REQUIRE(compareFolderContents(sourceTree, destTree));
}

struct CancellationTestObserver final : public ProgressObserver {
	inline void onCurrentFileChanged(const QString& /*file*/) override { fileProcessingStarted = true; }

	bool fileProcessingStarted = false;
};

// Cancels the operation while the first file is mid-copy and verifies that no data is lost:
// the file must survive intact in at least one of the two locations, and no partial file may be left at the destination.
static void cancellationTest(const Operation op)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	// A file spanning many chunks, with a partial chunk at the end
	QByteArray fileContents(100 * OPERATION_PERFORMER_CHUNK_SIZE + 512, '\0');
	for (int i = 0, n = static_cast<int>(fileContents.size()); i < n; ++i)
		fileContents[i] = static_cast<char>(i % 251); // 251 is coprime with the chunk size - no two chunks are identical

	const QString fileName = QStringLiteral("cancellation_test.bin");
	const QString sourceFilePath = sourceDirectory.path() % '/' % fileName;
	writeTestFile(sourceFilePath, fileContents);

	auto p = std::make_unique<COperationPerformer>(op, CFileSystemObject(sourceFilePath), targetDirectory.path());
	// Source and target are on the same drive; force the chunked copy+delete path for move - it's the one that can be canceled mid-file
	COperationPerformerTestSeam::setForceMoveByCopy(*p, true);

	auto observer = std::make_unique<CancellationTestObserver>();
	p->setObserver(observer.get());

	// Pause before starting: handlePause() precedes the first chunk copy, so the worker parks before copying anything
	REQUIRE(p->togglePause());
	p->start();

	pumpUntil(p, observer, [&observer] { return observer->fileProcessingStarted; });

	// The worker is committed to processing the file (past the loop's cancel check) but hasn't copied a single chunk yet.
	// Request cancellation, then let it run: it will copy exactly one chunk and then detect the cancellation - mid-file, deterministically.
	p->cancel();
	REQUIRE(!p->togglePause());

	pumpOperationToCompletion(p, observer);

	QFile sourceFile(sourceFilePath);
	QFile destFile(targetDirectory.path() % '/' % fileName);

	// No partial file may be left at the destination
	if (destFile.exists())
		REQUIRE(readFileContents(destFile.fileName()) == fileContents);

	// Canceling a copy must leave the source untouched; a canceled move must preserve the file in at least one location
	if (op == operationCopy)
		REQUIRE(sourceFile.exists());
	REQUIRE((sourceFile.exists() || destFile.exists()));

	if (sourceFile.exists())
		REQUIRE(readFileContents(sourceFilePath) == fileContents);
}

TEST_CASE((std::string("Copy test - empty files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(0);
}

TEST_CASE((std::string("Copy test - 5K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(5000);
}

TEST_CASE((std::string("Copy test - 20K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(20'000);
}

TEST_CASE((std::string("Move test - empty files #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	moveTest(0);
}

TEST_CASE((std::string("Move test - 5K files #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	moveTest(5000);
}

TEST_CASE((std::string("Move test - 20K files #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	moveTest(20'000);
}

TEST_CASE((std::string("Move cancellation test #") + std::to_string(rand())).c_str(), "[operationperformer-cancel]")
{
	cancellationTest(operationMove);
}

TEST_CASE((std::string("Copy cancellation test #") + std::to_string(rand())).c_str(), "[operationperformer-cancel]")
{
	cancellationTest(operationCopy);
}

struct OverwriteCancellationObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		CHECK(reason == hrFileExists);
		++promptCount;
		performer->userResponse(reason, urProceedWithThis);
	}

	COperationPerformer* performer = nullptr;
	int promptCount = 0;
};

static void overwriteCancellationTest(const Operation operation)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QByteArray sourceContents(100 * OPERATION_PERFORMER_CHUNK_SIZE + 512, 'A');
	const QByteArray destinationContents(3000, 'B');
	const QString fileName = QStringLiteral("overwrite-cancellation.bin");
	const QString sourcePath = sourceDirectory.path() % '/' % fileName;
	const QString destinationPath = targetDirectory.path() % '/' % fileName;
	writeTestFile(sourcePath, sourceContents);
	writeTestFile(destinationPath, destinationContents);

	auto p = std::make_unique<COperationPerformer>(operation, CFileSystemObject(sourcePath), targetDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*p, true);
	auto observer = std::make_unique<OverwriteCancellationObserver>();
	observer->performer = p.get();
	p->setObserver(observer.get());

	REQUIRE(p->togglePause());
	p->start();
	pumpUntil(p, observer, [&observer] { return observer->promptCount == 1; });
	p->cancel();
	REQUIRE(!p->togglePause());
	pumpOperationToCompletion(p, observer);

	REQUIRE(readFileContents(sourcePath) == sourceContents);
	REQUIRE(readFileContents(destinationPath) == destinationContents);
	REQUIRE(QDir(targetDirectory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == QStringList{ fileName });
}

TEST_CASE((std::string("Canceled copy overwrite preserves the destination #") + std::to_string(rand())).c_str(), "[operationperformer-cancel]")
{
	overwriteCancellationTest(operationCopy);
}

TEST_CASE((std::string("Canceled move overwrite preserves the destination #") + std::to_string(rand())).c_str(), "[operationperformer-cancel]")
{
	overwriteCancellationTest(operationMove);
}

struct BufferedEventsObserver final : public CFileOperationObserver {
	void onProgressChanged(float totalPercentage, size_t /*numFilesProcessed*/, size_t /*totalNumFiles*/, float /*filePercentage*/, uint64_t /*speed*/, uint32_t /*secondsRemaining*/) override {
		events.emplace_back('p');
		progressPercentages.emplace_back(totalPercentage);
	}

	void onProcessHalted(HaltReason /*reason*/, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		events.emplace_back('h');
	}

	void onProcessFinished(const QString& /*message*/) override {
		events.emplace_back('f');
	}

	void onCurrentFileChanged(const QString& file) override {
		events.emplace_back('c');
		currentFiles.emplace_back(file);
		if (cancellationToRequest)
			cancellationToRequest->store(true);
	}

	std::vector<char> events;
	std::vector<float> progressPercentages;
	std::vector<QString> currentFiles;
	std::shared_ptr<std::atomic<bool>> cancellationToRequest;
};

TEST_CASE("File-operation state events coalesce around ordering barriers", "[operationperformer-observer]")
{
	BufferedEventsObserver observer;
	auto cancellationRequested = std::make_shared<std::atomic<bool>>(false);

	CFileOperationObserverTestSeam::postCurrentFile(observer, QStringLiteral("first"));
	CFileOperationObserverTestSeam::postProgress(observer, 10.0f, 1, 10, 20.0f, 100, 9);
	CFileOperationObserverTestSeam::postCurrentFile(observer, QStringLiteral("second"));
	CFileOperationObserverTestSeam::postProgress(observer, 20.0f, 2, 10, 40.0f, 200, 8);
	CHECK(CFileOperationObserverTestSeam::pendingEventCount(observer) == 1);

	CFileOperationObserverTestSeam::postHalt(observer, hrFileExists, cancellationRequested);
	CFileOperationObserverTestSeam::postCurrentFile(observer, QStringLiteral("third"));
	CFileOperationObserverTestSeam::postProgress(observer, 30.0f, 3, 10, 60.0f, 300, 7);
	CFileOperationObserverTestSeam::postFinished(observer);
	CHECK(CFileOperationObserverTestSeam::pendingEventCount(observer) == 4);

	observer.processEvents();

	const std::vector<char> expectedEvents{ 'c', 'p', 'h', 'c', 'p', 'f' };
	CHECK(observer.events == expectedEvents);
	REQUIRE(observer.currentFiles.size() == 2);
	CHECK(observer.currentFiles[0] == QStringLiteral("second"));
	CHECK(observer.currentFiles[1] == QStringLiteral("third"));
	REQUIRE(observer.progressPercentages.size() == 2);
	CHECK(observer.progressPercentages[0] == 20.0f);
	CHECK(observer.progressPercentages[1] == 30.0f);
}

TEST_CASE("Cancellation suppresses a halt event already being dispatched", "[operationperformer-observer]")
{
	BufferedEventsObserver observer;
	auto cancellationRequested = std::make_shared<std::atomic<bool>>(false);
	observer.cancellationToRequest = cancellationRequested;

	CFileOperationObserverTestSeam::postCurrentFile(observer, QStringLiteral("cancel"));
	CFileOperationObserverTestSeam::postHalt(observer, hrFileExists, cancellationRequested);
	CFileOperationObserverTestSeam::postFinished(observer);
	observer.processEvents();

	const std::vector<char> expectedEvents{ 'c', 'f' };
	CHECK(observer.events == expectedEvents);
}

// Receives conflict prompts and deliberately leaves them unanswered, blocking the worker thread in waitForResponse()
struct UnansweredPromptObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		CHECK(reason == hrFileExists);
		++promptCount;
	}

	int promptCount = 0;
};

// Verifies that cancel() aborts the operation even while the worker thread is blocked waiting for a prompt response
TEST_CASE((std::string("Cancel during prompt test #") + std::to_string(rand())).c_str(), "[operationperformer-cancel]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	const QByteArray sourceContents(3000, 'A');
	const QByteArray destContents(4000, 'B');
	writeTestFile(sourceDirectory.path() % "/conflict.bin", sourceContents);
	writeTestFile(targetDirectory.path() % "/conflict.bin", destContents);

	auto p = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(sourceDirectory.path() % "/conflict.bin"), targetDirectory.path());
	auto observer = std::make_unique<UnansweredPromptObserver>();
	p->setObserver(observer.get());
	p->start();

	// Wait for the conflict prompt and leave it unanswered - the worker blocks in waitForResponse()
	pumpUntil(p, observer, [&observer] { return observer->promptCount > 0; });

	// cancel() must wake the blocked worker, and the operation must end as aborted
	p->cancel();
	pumpOperationToCompletion(p, observer);

	CHECK(observer->promptCount == 1);
	// The aborted operation must not have copied or overwritten anything
	REQUIRE(readFileContents(targetDirectory.path() % "/conflict.bin") == destContents);
	REQUIRE(readFileContents(sourceDirectory.path() % "/conflict.bin") == sourceContents);
}

// Answers hrFileExists prompts with a pre-defined sequence of responses, the way the real prompt dialog would
struct ScriptedResponsesObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		CHECK(reason == hrFileExists);
		if (scriptedResponses.empty())
		{
			FAIL_CHECK("A prompt occurred, but no scripted response is left for it");
			performer->userResponse(reason, urAbort, {}); // Must still respond, or the worker thread will wait forever
			return;
		}

		const auto [response, newName] = scriptedResponses.front();
		scriptedResponses.erase(scriptedResponses.begin());
		performer->userResponse(reason, response, newName);
	}

	COperationPerformer* performer = nullptr;
	std::vector<std::pair<UserResponse, QString>> scriptedResponses;
};

struct OverwriteConflictObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		CHECK(reason == hrFileExists);
		++promptCount;
		performer->userResponse(reason, urProceedWithThis);
	}

	COperationPerformer* performer = nullptr;
	int promptCount = 0;
};

TEST_CASE((std::string("Copy to a hard-link alias of the source is a no-op #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QByteArray contents(3000, 'A');
	const QString sourcePath = sourceDirectory.path() % "/file.bin";
	const QString aliasPath = targetDirectory.path() % "/file.bin";
	writeTestFile(sourcePath, contents);
	REQUIRE(createHardLink(sourcePath, aliasPath));

	const CFileSystemObject sourceObject(sourcePath);
	CFileManipulator manipulator(sourceObject);
	REQUIRE(manipulator.copyChunk(1024, targetDirectory.path() % '/') == FileOperationResultCode::Fail);
	REQUIRE(readFileContents(sourcePath) == contents);
	REQUIRE(readFileContents(aliasPath) == contents);

	auto p = std::make_unique<COperationPerformer>(operationCopy, sourceObject, targetDirectory.path());
	auto observer = std::make_unique<OverwriteConflictObserver>();
	observer->performer = p.get();
	p->setObserver(observer.get());
	p->start();
	pumpOperationToCompletion(p, observer);

	CHECK(observer->promptCount == 0);
	REQUIRE(readFileContents(sourcePath) == contents);
	REQUIRE(readFileContents(aliasPath) == contents);
}

enum class DestinationAlias { HardLink, SymbolicLink };

static void destinationAliasOverwriteTest(const Operation operation, const DestinationAlias aliasType)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	const QByteArray sourceContents(3000, 'A');
	const QByteArray linkTargetContents(4000, 'B');
	const QString sourcePath = sourceDirectory.path() % "/file.bin";
	const QString linkTargetPath = targetDirectory.path() % "/link-target.bin";
	const QString destinationPath = targetDirectory.path() % "/file.bin";
	writeTestFile(sourcePath, sourceContents);
	writeTestFile(linkTargetPath, linkTargetContents);

	if (aliasType == DestinationAlias::SymbolicLink)
	{
		REQUIRE(QFile::setPermissions(linkTargetPath, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther));
		REQUIRE(QFile::link(linkTargetPath, destinationPath));
	}
	else
		REQUIRE(createHardLink(linkTargetPath, destinationPath));

	auto p = std::make_unique<COperationPerformer>(operation, CFileSystemObject(sourcePath), targetDirectory.path());
	if (operation == operationMove)
		COperationPerformerTestSeam::setForceMoveByCopy(*p, true);

	auto observer = std::make_unique<OverwriteConflictObserver>();
	observer->performer = p.get();
	p->setObserver(observer.get());
	p->start();
	pumpOperationToCompletion(p, observer);

	CHECK(observer->promptCount == 1);
	REQUIRE(readFileContents(destinationPath) == sourceContents);
	REQUIRE(readFileContents(linkTargetPath) == linkTargetContents);
	REQUIRE(QFileInfo(destinationPath).isSymbolicLink() == false);
	REQUIRE(QFileInfo::exists(sourcePath) == (operation == operationCopy));
	if (aliasType == DestinationAlias::SymbolicLink)
		REQUIRE((QFileInfo(linkTargetPath).permissions() & (QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther)) == 0);
}

TEST_CASE((std::string("Copy overwrite replaces a destination hard-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	destinationAliasOverwriteTest(operationCopy, DestinationAlias::HardLink);
}

TEST_CASE((std::string("Move by copy overwrite replaces a destination hard-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	destinationAliasOverwriteTest(operationMove, DestinationAlias::HardLink);
}

#ifndef _WIN32
TEST_CASE((std::string("Copy overwrite replaces a destination symbolic-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	destinationAliasOverwriteTest(operationCopy, DestinationAlias::SymbolicLink);
}

TEST_CASE((std::string("Move by copy overwrite replaces a destination symbolic-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	destinationAliasOverwriteTest(operationMove, DestinationAlias::SymbolicLink);
}
#endif

// Verifies the urRename response handling: the renamed destination must undergo the same conflict checks (i.e. prompt again if taken),
// and the new name must only apply to the item it was given for, without leaking to the subsequent items
TEST_CASE((std::string("Copy rename conflict test #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	// Two files to copy; the first one conflicts at the destination, and so does the first rename attempt for it
	const QByteArray contentsA(3000, 'A');
	const QByteArray contentsB(4000, 'B');
	const QByteArray contentsExisting(5000, 'C');
	const QByteArray contentsTaken(6000, 'D');
	writeTestFile(sourceDirectory.path() % "/a_conflict.bin", contentsA);
	writeTestFile(sourceDirectory.path() % "/b_normal.bin", contentsB);
	writeTestFile(targetDirectory.path() % "/a_conflict.bin", contentsExisting);
	writeTestFile(targetDirectory.path() % "/taken.bin", contentsTaken);

	std::vector<CFileSystemObject> source;
	source.emplace_back(sourceDirectory.path() % "/a_conflict.bin");
	source.emplace_back(sourceDirectory.path() % "/b_normal.bin");

	auto p = std::make_unique<COperationPerformer>(operationCopy, std::move(source), targetDirectory.path());

	auto observer = std::make_unique<ScriptedResponsesObserver>();
	observer->performer = p.get();
	observer->scriptedResponses = { {urRename, QStringLiteral("taken.bin")}, {urRename, QStringLiteral("renamed.bin")} };
	p->setObserver(observer.get());

	p->start();
	pumpOperationToCompletion(p, observer);

	REQUIRE(observer->scriptedResponses.empty()); // Both prompts must have occurred

	// The first file ended up under the second new name, and the conflicting files are untouched
	REQUIRE(readFileContents(targetDirectory.path() % "/renamed.bin") == contentsA);
	REQUIRE(readFileContents(targetDirectory.path() % "/a_conflict.bin") == contentsExisting);
	REQUIRE(readFileContents(targetDirectory.path() % "/taken.bin") == contentsTaken);
	// The second file was copied under its own name - the rename must not leak to it
	REQUIRE(readFileContents(targetDirectory.path() % "/b_normal.bin") == contentsB);
	// The sources are untouched
	REQUIRE(readFileContents(sourceDirectory.path() % "/a_conflict.bin") == contentsA);
	REQUIRE(readFileContents(sourceDirectory.path() % "/b_normal.bin") == contentsB);
}

// Verifies the urProceedWithThis / urProceedWithAll (overwrite) response handling for a same-drive move:
// the conflicting destination files must be replaced with the source files, and "... with all" must be remembered (no further prompts)
TEST_CASE((std::string("Move overwrite conflict test #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	// All three files conflict at the destination
	const QByteArray contents1(3000, 'A');
	const QByteArray contents2(4000, 'B');
	const QByteArray contents3(5000, 'C');
	writeTestFile(sourceDirectory.path() % "/file1.bin", contents1);
	writeTestFile(sourceDirectory.path() % "/file2.bin", contents2);
	writeTestFile(sourceDirectory.path() % "/file3.bin", contents3);
	writeTestFile(targetDirectory.path() % "/file1.bin", QByteArray(1000, 'X'));
	writeTestFile(targetDirectory.path() % "/file2.bin", QByteArray(1100, 'Y'));
	writeTestFile(targetDirectory.path() % "/file3.bin", QByteArray(1200, 'Z'));

	std::vector<CFileSystemObject> source;
	source.emplace_back(sourceDirectory.path() % "/file1.bin");
	source.emplace_back(sourceDirectory.path() % "/file2.bin");
	source.emplace_back(sourceDirectory.path() % "/file3.bin");

	// Source and target are on the same drive - this exercises the moveWithinSameDrive() path
	auto p = std::make_unique<COperationPerformer>(operationMove, std::move(source), targetDirectory.path());

	auto observer = std::make_unique<ScriptedResponsesObserver>();
	observer->performer = p.get();
	// The third conflict must be resolved by the remembered "proceed with all" without a prompt
	observer->scriptedResponses = { {urProceedWithThis, {}}, {urProceedWithAll, {}} };
	p->setObserver(observer.get());

	p->start();
	pumpOperationToCompletion(p, observer);

	REQUIRE(observer->scriptedResponses.empty());

	// The files were moved: the conflicting destination files are replaced, the sources are gone
	REQUIRE(readFileContents(targetDirectory.path() % "/file1.bin") == contents1);
	REQUIRE(readFileContents(targetDirectory.path() % "/file2.bin") == contents2);
	REQUIRE(readFileContents(targetDirectory.path() % "/file3.bin") == contents3);
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/file1.bin"));
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/file2.bin"));
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/file3.bin"));
}

// Regression test for a same-drive move to a non-existent destination folder: the folder must be created and the items placed
// INSIDE it, not dumped into its parent. Source and target are on the same drive, so this exercises the moveWithinSameDrive() path.
TEST_CASE((std::string("Move to non-existent folder test #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	const QByteArray contents1(3000, 'A');
	const QByteArray contents2(4000, 'B');
	writeTestFile(sourceDirectory.path() % "/file1.bin", contents1);
	writeTestFile(sourceDirectory.path() % "/file2.bin", contents2);

	// A subfolder of the target that does not exist yet
	const QString destFolder = targetDirectory.path() % "/new_subfolder";

	std::vector<CFileSystemObject> source;
	source.emplace_back(sourceDirectory.path() % "/file1.bin");
	source.emplace_back(sourceDirectory.path() % "/file2.bin");

	auto p = std::make_unique<COperationPerformer>(operationMove, std::move(source), destFolder);
	auto observer = std::make_unique<ProgressObserver>(); // No prompts are expected - ProgressObserver fails the test on any halt
	p->setObserver(observer.get());
	p->start();
	pumpOperationToCompletion(p, observer);

	// The destination folder was created and both files landed inside it
	REQUIRE(QFileInfo(destFolder).isDir());
	REQUIRE(readFileContents(destFolder % "/file1.bin") == contents1);
	REQUIRE(readFileContents(destFolder % "/file2.bin") == contents2);
	// The sources are gone
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/file1.bin"));
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/file2.bin"));
	// Regression guard: the old code dumped the files into the parent instead of creating the subfolder
	REQUIRE(!QFileInfo::exists(targetDirectory.path() % "/file1.bin"));
	REQUIRE(!QFileInfo::exists(targetDirectory.path() % "/file2.bin"));
}

// Regression test for a same-drive move-and-rename of a single file into a non-existent folder: the folder is created and the file renamed into it
TEST_CASE((std::string("Move-rename into non-existent folder test #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	const QByteArray contents(3000, 'A');
	writeTestFile(sourceDirectory.path() % "/original.bin", contents);

	// A single file moved onto a full path whose folder does not exist yet
	const QString destFolder = targetDirectory.path() % "/new_subfolder";
	const QString destFilePath = destFolder % "/renamed.bin";

	auto p = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(sourceDirectory.path() % "/original.bin"), destFilePath);
	auto observer = std::make_unique<ProgressObserver>();
	p->setObserver(observer.get());
	p->start();
	pumpOperationToCompletion(p, observer);

	// The folder was created and the file was renamed into it
	REQUIRE(QFileInfo(destFolder).isDir());
	REQUIRE(readFileContents(destFilePath) == contents);
	REQUIRE(!QFileInfo::exists(sourceDirectory.path() % "/original.bin"));
}

TEST_CASE((std::string("Delete test #") + std::to_string(rand())).c_str(), "[operationperformer-delete]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	REQUIRE(sourceDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, 20000));
	REQUIRE(!QDir(sourceDirectory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).empty());

	ProgressObserver progressObserver;
	COperationPerformer p(operationDelete, CFileSystemObject(sourceDirectory.path()));
	p.setObserver(&progressObserver);

	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	REQUIRE(!CFileSystemObject(sourceDirectory.path()).exists());
}

int main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

	// Build a new parser on top of Catch's
	using namespace Catch::clara;
	auto cli
		= session.cli() // Get Catch's composite command line parser
		| Opt(g_randomSeed, "std::random seed") // bind variable to a new option, with a hint string
		["--std-seed"]        // the option names it will respond to
		("std::random seed"); // description string for the help output

	// Now pass the new composite back to Catch so it uses that
	session.cli(cli);

	// Let Catch (using Clara) parse the command line
	const int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	srand(g_randomSeed);

	{
		CRandomDataGenerator _randomGenerator;
		_randomGenerator.setSeed(g_randomSeed);
		Logger() << "RNG consustency check: seed = " << g_randomSeed <<", first RN = " << _randomGenerator.randomNumber<uint32_t>(0u, uint32_max);
	}

	return session.run();
}
