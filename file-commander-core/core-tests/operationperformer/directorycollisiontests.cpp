#include "operationperformertesthelpers.h"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QStringBuilder>
#include <QStringList>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <string>

struct DirectoryCollisionSetup {
	DirectoryCollisionSetup() :
		sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX"),
		targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX"),
		sourceRoot(sourceDirectory.path()),
		destinationRoot(targetDirectory.path() % '/' % CFileSystemObject(sourceRoot).fullName())
	{
		REQUIRE(sourceDirectory.isValid());
		REQUIRE(targetDirectory.isValid());
		REQUIRE(QDir{}.mkpath(sourceRoot % "/nested/empty"));
		writeTestFile(sourceRoot % "/root.bin", rootContents);
		writeTestFile(sourceRoot % "/nested/nested.bin", nestedContents);
		writeTestFile(destinationRoot, collisionContents);
	}

	QTemporaryDir sourceDirectory;
	QTemporaryDir targetDirectory;
	const QString sourceRoot;
	const QString destinationRoot;
	const QByteArray rootContents = QByteArray(2000, 'R');
	const QByteArray nestedContents = QByteArray(3000, 'N');
	const QByteArray collisionContents = QByteArray(1000, 'C');
};

static void overwriteDirectoryToFileCollision(const Operation operation, const bool forceMoveByCopy)
{
	DirectoryCollisionSetup setup;
	if (operation == operationMove && !forceMoveByCopy)
		REQUIRE(CFileSystemObject(setup.sourceRoot).isMovableTo(CFileSystemObject(setup.targetDirectory.path())));

	auto performer = std::make_unique<COperationPerformer>(operation, CFileSystemObject(setup.sourceRoot), setup.targetDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*performer, forceMoveByCopy);
	auto observer = std::make_unique<ScriptedResponsesObserver>();
	observer->performer = performer.get();
	observer->scriptedResponses = { {urProceedWithThis, {}} };
	performer->setObserver(observer.get());
	performer->start();
	pumpOperationToCompletion(performer, observer);

	REQUIRE(observer->scriptedResponses.empty());
	REQUIRE(QFileInfo(setup.destinationRoot).isDir());
	REQUIRE(readFileContents(setup.destinationRoot % "/root.bin") == setup.rootContents);
	REQUIRE(readFileContents(setup.destinationRoot % "/nested/nested.bin") == setup.nestedContents);
	REQUIRE(QFileInfo(setup.destinationRoot % "/nested/empty").isDir());
	REQUIRE(QFileInfo::exists(setup.sourceRoot) == (operation == operationCopy));
	if (operation == operationCopy)
		REQUIRE(readFileContents(setup.sourceRoot % "/nested/nested.bin") == setup.nestedContents);
}

TEST_CASE((std::string("Copying a directory can replace a destination file #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	overwriteDirectoryToFileCollision(operationCopy, false);
}

TEST_CASE((std::string("Moving a directory by copy can replace a destination file #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	overwriteDirectoryToFileCollision(operationMove, true);
}

TEST_CASE((std::string("Same-drive directory move can replace a destination file #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	overwriteDirectoryToFileCollision(operationMove, false);
}

TEST_CASE((std::string("Renaming a colliding directory rebases its destination subtree #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	DirectoryCollisionSetup setup;
	const QString takenPath = setup.targetDirectory.path() % "/taken-directory-name";
	const QString renamedRoot = setup.targetDirectory.path() % "/renamed-directory";
	const QByteArray takenContents(1500, 'T');
	writeTestFile(takenPath, takenContents);

	auto performer = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(setup.sourceRoot), setup.targetDirectory.path());
	auto observer = std::make_unique<ScriptedResponsesObserver>();
	observer->performer = performer.get();
	observer->scriptedResponses = { {urRename, QStringLiteral("taken-directory-name")}, {urRename, QStringLiteral("renamed-directory")} };
	performer->setObserver(observer.get());
	performer->start();
	pumpOperationToCompletion(performer, observer);

	REQUIRE(observer->scriptedResponses.empty());
	REQUIRE(readFileContents(setup.destinationRoot) == setup.collisionContents);
	REQUIRE(readFileContents(takenPath) == takenContents);
	REQUIRE(readFileContents(renamedRoot % "/nested/nested.bin") == setup.nestedContents);
	REQUIRE(readFileContents(setup.sourceRoot % "/nested/nested.bin") == setup.nestedContents);
}

TEST_CASE((std::string("Skipping a colliding directory skips its entire subtree #") + std::to_string(rand())).c_str(), "[operationperformer-conflict]")
{
	DirectoryCollisionSetup setup;

	auto performer = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(setup.sourceRoot), setup.targetDirectory.path());
	auto observer = std::make_unique<ScriptedResponsesObserver>();
	observer->performer = performer.get();
	observer->scriptedResponses = { {urSkipThis, {}} };
	performer->setObserver(observer.get());
	performer->start();
	pumpOperationToCompletion(performer, observer);

	REQUIRE(observer->scriptedResponses.empty());
	REQUIRE(readFileContents(setup.destinationRoot) == setup.collisionContents);
	REQUIRE(readFileContents(setup.sourceRoot % "/nested/nested.bin") == setup.nestedContents);
	REQUIRE(QDir(setup.targetDirectory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot) == QStringList{ CFileSystemObject(setup.sourceRoot).fullName() });
}
