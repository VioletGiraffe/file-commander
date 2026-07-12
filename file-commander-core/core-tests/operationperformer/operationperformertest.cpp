#include "fileoperations/coperationperformer.h"
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

#define CATCH_CONFIG_RUNNER
#include "3rdparty/catch2/catch.hpp"
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

struct ProgressObserver : public CFileOperationObserver {
	inline void onProgressChanged(float totalPercentage, size_t /*numFilesProcessed*/, size_t /*totalNumFiles*/, float filePercentage, uint64_t /*speed*/ /* B/s*/, uint32_t /*secondsRemaining*/) override {
		CHECK(totalPercentage <= 100.1f);
		CHECK(filePercentage <= 100.1f);
	}
	inline void onProcessHalted(HaltReason /*reason*/, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override { // User decision required (file exists, file is read-only etc.)
		FAIL("onProcessHalted called");
	}
	inline void onProcessFinished(const QString& /*message*/ = QString()) override {} // Done or canceled
	inline void onCurrentFileChanged(const QString& /*file*/) override {} // Starting to process a new file
};

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

	COperationPerformer p(operationCopy, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	CTimeElapsed timer(true);

	ProgressObserver progressObserver;
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

	COperationPerformer p(operationMove, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	ProgressObserver progressObserver;
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

static void writeTestFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

static QByteArray readFileContents(const QString& path)
{
	QFile file(path);
	REQUIRE(file.open(QFile::ReadOnly));
	return file.readAll();
}

// Pumps the observer's event queue until the condition becomes true.
// On timeout, deliberately leaks the performer and the observer - the worker thread is likely stuck, and ~COperationPerformer
// would hang forever trying to join it - and fails the test, so that a hang is flagged red in-process.
template <typename ObserverT, typename ConditionT>
static void pumpUntil(std::unique_ptr<COperationPerformer>& p, std::unique_ptr<ObserverT>& observer, ConditionT&& condition)
{
	CTimeElapsed timer(true);
	while (!condition())
	{
		observer->processEvents();
		if (timer.elapsed<std::chrono::seconds>() > 30)
		{
			(void)p.release();
			(void)observer.release();
			FAIL("File operation timeout reached - the worker thread is likely stuck.");
		}
	}
}

template <typename ObserverT>
static void pumpOperationToCompletion(std::unique_ptr<COperationPerformer>& p, std::unique_ptr<ObserverT>& observer)
{
	pumpUntil(p, observer, [&p] { return p->done(); });
}

struct CancellationTestObserver final : public ProgressObserver {
	inline void onCurrentFileChanged(const QString& /*file*/) override { fileProcessingStarted = true; }

	bool fileProcessingStarted = false;
};

// Friend of COperationPerformer
struct COperationPerformerTestSeam {
	static void setForceMoveByCopy(COperationPerformer& p, const bool force) { p._forceMoveByCopy = force; }
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

	COperationPerformer p(operationDelete, CFileSystemObject(sourceDirectory.path()));
	ProgressObserver progressObserver;
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
