// File-link handling: symbolic links are copied through as regular file contents, while delete and the cleanup phase of
// copy-based move remove only the link entry. Destination links are replaced as entries when overwriting.

#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <string>

struct OverwriteConflictObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		CHECK(reason == hrFileExists);
		++promptCount;
		performer->userResponse(reason, urProceedWithThis);
	}

	COperationPerformer* performer = nullptr;
	int promptCount = 0;
};

TEST_CASE((std::string("Copy to a hard-link alias of the source is a no-op #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
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

TEST_CASE((std::string("Copy overwrite replaces a destination hard-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	destinationAliasOverwriteTest(operationCopy, DestinationAlias::HardLink);
}

TEST_CASE((std::string("Move by copy overwrite replaces a destination hard-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	destinationAliasOverwriteTest(operationMove, DestinationAlias::HardLink);
}

#ifndef _WIN32
static constexpr QFile::Permissions readOnlyPermissions = QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther;
static constexpr QFile::Permissions writePermissions = QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther;

static void requireFileIsReadOnly(const QString& path)
{
	REQUIRE((QFileInfo(path).permissions() & writePermissions) == 0);
}

TEST_CASE((std::string("Copy overwrite replaces a destination symbolic-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	destinationAliasOverwriteTest(operationCopy, DestinationAlias::SymbolicLink);
}

TEST_CASE((std::string("Move by copy overwrite replaces a destination symbolic-link entry #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	destinationAliasOverwriteTest(operationMove, DestinationAlias::SymbolicLink);
}

TEST_CASE((std::string("Deleting a file link never changes its read-only target #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	const QByteArray targetContents(3000, 'T');
	const QString targetPath = linkTargetDirectory.path() % "/target.bin";
	const QString linkPath = sourceDirectory.path() % "/file_link";
	writeTestFile(targetPath, targetContents);
	REQUIRE(QFile::setPermissions(targetPath, readOnlyPermissions));
	REQUIRE(QFile::link(targetPath, linkPath));
	REQUIRE(CFileSystemObject(linkPath).isLink());
	REQUIRE(!CFileSystemObject(linkPath).isWriteable());

	auto p = std::make_unique<COperationPerformer>(operationDelete, CFileSystemObject(linkPath));
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	REQUIRE(!QFileInfo(linkPath).isSymbolicLink());
	REQUIRE(readFileContents(targetPath) == targetContents);
	requireFileIsReadOnly(targetPath);
}

TEST_CASE((std::string("Deleting a broken file link removes it without a missing-file prompt #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	REQUIRE(sourceDirectory.isValid());

	const QString missingTargetPath = sourceDirectory.path() % "/missing-target.bin";
	const QString linkPath = sourceDirectory.path() % "/broken_link";
	REQUIRE(QFile::link(missingTargetPath, linkPath));
	const CFileSystemObject linkObject(linkPath);
	REQUIRE(linkObject.isLink());
	REQUIRE(linkObject.exists());
	REQUIRE(linkObject.isFile());

	auto p = std::make_unique<COperationPerformer>(operationDelete, linkObject);
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	REQUIRE(!QFileInfo(linkPath).isSymbolicLink());
}

TEST_CASE((std::string("Move by copy materializes a file link without changing its read-only target #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir destinationDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_DEST_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(destinationDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	const QByteArray targetContents(3000, 'T');
	const QString targetPath = linkTargetDirectory.path() % "/target.bin";
	const QString linkPath = sourceDirectory.path() % "/file_link";
	const QString destinationPath = destinationDirectory.path() % "/file_link";
	writeTestFile(targetPath, targetContents);
	REQUIRE(QFile::setPermissions(targetPath, readOnlyPermissions));
	REQUIRE(QFile::link(targetPath, linkPath));
	REQUIRE(CFileSystemObject(linkPath).isLink());
	REQUIRE(!CFileSystemObject(linkPath).isWriteable());

	auto p = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(linkPath), destinationDirectory.path());
	COperationPerformerTestSeam::setForceMoveByCopy(*p, true);
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	REQUIRE(!QFileInfo(linkPath).isSymbolicLink());
	REQUIRE(QFileInfo(destinationPath).isFile());
	REQUIRE(!QFileInfo(destinationPath).isSymbolicLink());
	REQUIRE(readFileContents(destinationPath) == targetContents);
	REQUIRE(readFileContents(targetPath) == targetContents);
	requireFileIsReadOnly(targetPath);
}
#endif
