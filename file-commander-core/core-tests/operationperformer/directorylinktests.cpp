// Directory link handling: symlinks on POSIX, junctions on Windows.
// Intended semantics: copy materializes the linked content as real files and folders;
// delete removes the link itself and must never touch the target's contents.

#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#ifdef _WIN32
#include <QProcess>
#endif
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <string>

// Creates the platform's common directory link type: a symlink on POSIX, a junction on Windows
// (junction creation, unlike symlink creation, requires no special privileges on Windows)
static bool createDirectoryLink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return QProcess::execute(QStringLiteral("cmd"), { QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
		QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(targetPath) }) == 0;
#else
	return QFile::link(targetPath, linkPath);
#endif
}

static bool isDirectoryLink(const QString& path)
{
	const QFileInfo info(path);
	return info.isSymLink() || info.isJunction();
}

// The link target tree that operations on a link must never modify; doubles as the expected result of materializing the link
static void populateLinkTargetDir(const QString& path)
{
	REQUIRE(QDir{ path }.mkpath(QStringLiteral("sub")));
	writeTestFile(path % "/victim1.bin", QByteArray(3000, 'V'));
	writeTestFile(path % "/sub/victim2.bin", QByteArray(4000, 'W'));
}

static void requireLinkTargetContentsIntact(const QString& path)
{
	REQUIRE(readFileContents(path % "/victim1.bin") == QByteArray(3000, 'V'));
	REQUIRE(readFileContents(path % "/sub/victim2.bin") == QByteArray(4000, 'W'));
}

// Responds to any prompt with urAbort so that a failing operation can't hang the worker thread
struct AutoAbortObserver final : public ProgressObserver {
	inline void onProcessHalted(HaltReason reason, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override {
		++promptCount;
		performer->userResponse(reason, urAbort, {});
	}

	COperationPerformer* performer = nullptr;
	int promptCount = 0;
};

// Runs the operation to completion, auto-aborting on any prompt, and returns the number of prompts that occurred
// (correct link handling must complete without a single prompt).
// Takes ownership of the performer: _done is set before the worker's final observer callback,
// so the thread must be joined (by destroying the performer) while the observer is still alive.
static int runOperationAutoAbortingPrompts(std::unique_ptr<COperationPerformer> p)
{
	auto observer = std::make_unique<AutoAbortObserver>();
	observer->performer = p.get();
	p->setObserver(observer.get());
	p->start();
	pumpOperationToCompletion(p, observer);
	p.reset();
	return observer->promptCount;
}

TEST_CASE((std::string("Delete must not follow directory links #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	writeTestFile(sourceDirectory.path() % "/real.bin", QByteArray(1000, 'R'));
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), sourceDirectory.path() % "/dir_link"));

	auto p = std::make_unique<COperationPerformer>(operationDelete, CFileSystemObject(sourceDirectory.path()));
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	// The source dir is gone, link included; the link target and all its contents are untouched
	REQUIRE(!QFileInfo::exists(sourceDirectory.path()));
	requireLinkTargetContentsIntact(linkTargetDirectory.path());
}

TEST_CASE((std::string("Deleting a directory link removes the link, not the target #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	const QString linkPath = sourceDirectory.path() % "/dir_link";
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), linkPath));

	// The link itself is the item being deleted
	auto p = std::make_unique<COperationPerformer>(operationDelete, CFileSystemObject(linkPath));
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	REQUIRE(!QFileInfo::exists(linkPath));
	REQUIRE(!isDirectoryLink(linkPath));
	requireLinkTargetContentsIntact(linkTargetDirectory.path());
}

TEST_CASE((std::string("Copy materializes a directory link as real content #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir destinationDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_DEST_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(destinationDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Destination: " << destinationDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	const QByteArray realContents(1000, 'R');
	writeTestFile(sourceDirectory.path() % "/real.bin", realContents);
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), sourceDirectory.path() % "/dir_link"));

	auto p = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(sourceDirectory.path()), destinationDirectory.path());
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	const QString destRoot = destinationDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName();
	REQUIRE(readFileContents(destRoot % "/real.bin") == realContents);

	// The link arrives as a real folder with the target's content, not as a link
	const QString materializedDir = destRoot % "/dir_link";
	REQUIRE(QFileInfo(materializedDir).isDir());
	REQUIRE(!isDirectoryLink(materializedDir));
	requireLinkTargetContentsIntact(materializedDir);

	// The source link and its target are untouched
	REQUIRE(isDirectoryLink(sourceDirectory.path() % "/dir_link"));
	requireLinkTargetContentsIntact(linkTargetDirectory.path());
}

TEST_CASE((std::string("Move by copy+delete materializes the link and must not touch the link target #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir destinationDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_DEST_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(destinationDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Destination: " << destinationDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	const QByteArray realContents(1000, 'R');
	writeTestFile(sourceDirectory.path() % "/real.bin", realContents);
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), sourceDirectory.path() % "/dir_link"));

	auto p = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(sourceDirectory.path()), destinationDirectory.path());
	// Same drive in this test, so the copy+delete path (the dangerous one for links) must be forced
	COperationPerformerTestSeam::setForceMoveByCopy(*p, true);
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	// The critical check: the delete phase removed only the link, never the target's contents
	requireLinkTargetContentsIntact(linkTargetDirectory.path());

	const QString destRoot = destinationDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName();
	REQUIRE(readFileContents(destRoot % "/real.bin") == realContents);
	REQUIRE(QFileInfo(destRoot % "/dir_link").isDir());
	REQUIRE(!isDirectoryLink(destRoot % "/dir_link"));
	requireLinkTargetContentsIntact(destRoot % "/dir_link");

	REQUIRE(!QFileInfo::exists(sourceDirectory.path()));
}

TEST_CASE((std::string("Same-drive move of a directory link moves the link itself #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir destinationDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_DEST_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(destinationDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Destination: " << destinationDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	const QString linkPath = sourceDirectory.path() % "/dir_link";
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), linkPath));

	// Same drive, no force: the rename path moves the link as a link, without materializing it
	auto p = std::make_unique<COperationPerformer>(operationMove, CFileSystemObject(linkPath), destinationDirectory.path());
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	const QString movedLinkPath = destinationDirectory.path() % "/dir_link";
	REQUIRE(isDirectoryLink(movedLinkPath));
	REQUIRE(!QFileInfo::exists(linkPath));
	requireLinkTargetContentsIntact(linkTargetDirectory.path());
	requireLinkTargetContentsIntact(movedLinkPath); // The target's content is reachable through the moved link
}

TEST_CASE((std::string("Copy of a tree with a link cycle terminates without runaway recursion #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir destinationDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_DEST_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(destinationDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Destination: " << destinationDirectory.path();

	const QByteArray realContents(1000, 'R');
	writeTestFile(sourceDirectory.path() % "/real.bin", realContents);
	REQUIRE(QDir{ sourceDirectory.path() }.mkpath(QStringLiteral("sub")));
	// A link pointing back at an ancestor of its own location
	REQUIRE(createDirectoryLink(sourceDirectory.path(), sourceDirectory.path() % "/sub/link_up"));

	// pumpOperationToCompletion() inside flags a hang as a test failure via its watchdog timeout
	auto p = std::make_unique<COperationPerformer>(operationCopy, CFileSystemObject(sourceDirectory.path()), destinationDirectory.path());
	REQUIRE(runOperationAutoAbortingPrompts(std::move(p)) == 0);

	const QString destRoot = destinationDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName();
	REQUIRE(readFileContents(destRoot % "/real.bin") == realContents);
	// Whether the cycle link is skipped or materialized as an empty folder, no recursive expansion may occur
	REQUIRE(!QFileInfo::exists(destRoot % "/sub/link_up/sub"));

	// The source is untouched
	REQUIRE(readFileContents(sourceDirectory.path() % "/real.bin") == realContents);
	REQUIRE(isDirectoryLink(sourceDirectory.path() % "/sub/link_up"));
}

TEST_CASE((std::string("CFileManipulator::remove() on a directory link removes only the link #") + std::to_string(rand())).c_str(), "[operationperformer-links]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir linkTargetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_LINKTARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(linkTargetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Link target: " << linkTargetDirectory.path();

	populateLinkTargetDir(linkTargetDirectory.path());
	const QString linkPath = sourceDirectory.path() % "/dir_link";
	REQUIRE(createDirectoryLink(linkTargetDirectory.path(), linkPath));

	CFileManipulator manipulator{ CFileSystemObject(linkPath) };
	REQUIRE(manipulator.remove() == FileOperationResultCode::Ok);

	REQUIRE(!QFileInfo::exists(linkPath));
	requireLinkTargetContentsIntact(linkTargetDirectory.path());
}
