// WP10C: the UI-side launch boundary - the preserved destination-intent heuristic, the confirmation-field
// prefill, request construction and validation, and the platform deletion-backend selection.

#include "progressdialogs/fileoperationlaunch.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

namespace
{

void makeFile(const QString& path)
{
	QFile file{ path };
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write("x") == 1);
}

} // namespace

TEST_CASE("launch: destination-intent heuristic", "[fileoperationlaunch]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	makeFile(base % "/a.txt");
	makeFile(base % "/b.txt");
	REQUIRE(QDir{}.mkpath(base % "/sub"));
	REQUIRE(QDir{}.mkpath(base % "/existingDir"));

	using enum DestinationIntent;

	SECTION("multiple sources always map into the destination directory")
	{
		CHECK(transferDestinationIntent({ base % "/a.txt", base % "/b.txt" }, base % "/existingDir") == IntoDirectory);
	}

	SECTION("a single directory source maps into the destination directory")
	{
		CHECK(transferDestinationIntent({ base % "/sub" }, base % "/existingDir") == IntoDirectory);
	}

	SECTION("a single file with a destination that already exists as a directory maps into it")
	{
		CHECK(transferDestinationIntent({ base % "/a.txt" }, base % "/existingDir") == IntoDirectory);
	}

	SECTION("a single file with an absent destination is an exact target")
	{
		CHECK(transferDestinationIntent({ base % "/a.txt" }, base % "/renamed.txt") == ExactEntry);
	}

	SECTION("a single file with a destination that exists as a file is an exact target")
	{
		CHECK(transferDestinationIntent({ base % "/a.txt" }, base % "/b.txt") == ExactEntry);
	}
}

TEST_CASE("launch: confirmation-field prefill", "[fileoperationlaunch]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	makeFile(base % "/a.txt");
	REQUIRE(QDir{}.mkpath(base % "/sub"));
	const QString destDir = base % "/dest";

	SECTION("a one-file copy prefills the exact target path")
	{
		const QString prefilled = prefillTransferDestination(TransferKind::Copy, { base % "/a.txt" }, destDir);
		CHECK(QDir{ prefilled }.dirName() == QStringLiteral("a.txt"));
		CHECK(QFileInfo{ prefilled }.dir().absolutePath() == QDir{ destDir }.absolutePath());
	}

	SECTION("a one-file move prefills the directory")
	{
		CHECK(prefillTransferDestination(TransferKind::Move, { base % "/a.txt" }, destDir) == destDir);
	}

	SECTION("a single directory prefills the directory even for copy")
	{
		CHECK(prefillTransferDestination(TransferKind::Copy, { base % "/sub" }, destDir) == destDir);
	}

	SECTION("multiple sources prefill the directory")
	{
		CHECK(prefillTransferDestination(TransferKind::Copy, { base % "/a.txt", base % "/sub" }, destDir) == destDir);
	}
}

TEST_CASE("launch: request construction chooses the intent once", "[fileoperationlaunch]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	makeFile(base % "/a.txt");
	makeFile(base % "/b.txt");
	REQUIRE(QDir{}.mkpath(base % "/existingDir"));

	SECTION("a file into an existing directory becomes an IntoDirectory request")
	{
		const auto request = makeUiTransferRequest(TransferKind::Copy, { base % "/a.txt" }, base % "/existingDir");
		REQUIRE(request.has_value());
		CHECK(request->kind == TransferKind::Copy);
		CHECK(request->destination.intent == DestinationIntent::IntoDirectory);
		REQUIRE(request->sources.size() == 1);
	}

	SECTION("a file to an absent name becomes an ExactEntry request")
	{
		const auto request = makeUiTransferRequest(TransferKind::Move, { base % "/a.txt" }, base % "/moved.txt");
		REQUIRE(request.has_value());
		CHECK(request->destination.intent == DestinationIntent::ExactEntry);
	}

	SECTION("multiple sources are always IntoDirectory, never ExactEntry")
	{
		const auto request = makeUiTransferRequest(TransferKind::Copy, { base % "/a.txt", base % "/b.txt" }, base % "/existingDir");
		REQUIRE(request.has_value());
		CHECK(request->destination.intent == DestinationIntent::IntoDirectory);
		CHECK(request->sources.size() == 2);
	}

	SECTION("the synthetic parent entry is filtered before request creation")
	{
		const auto request = makeUiTransferRequest(TransferKind::Copy, { base % "/a.txt", QStringLiteral("..") }, base % "/existingDir");
		REQUIRE(request.has_value());
		CHECK(request->sources.size() == 1); // Only a.txt survives
	}

	SECTION("a selection reduced to nothing fails before request creation")
	{
		const auto request = makeUiTransferRequest(TransferKind::Copy, { QStringLiteral("..") }, base % "/existingDir");
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::NoSources);
	}

	SECTION("a relative source path is rejected")
	{
		const auto request = makeUiTransferRequest(TransferKind::Copy, { QStringLiteral("relative/path.txt") }, base % "/existingDir");
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::InvalidPath);
	}

	SECTION("a filesystem root is rejected as a source")
	{
#ifdef _WIN32
		const QString root = QStringLiteral("C:/");
#else
		const QString root = QStringLiteral("/");
#endif
		const auto request = makeUiTransferRequest(TransferKind::Copy, { root }, base % "/existingDir");
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::RootSource);
	}
}

TEST_CASE("launch: deletion backend selection", "[fileoperationlaunch]")
{
	SECTION("trash")
	{
#if defined _WIN32 || defined __APPLE__
		CHECK(deletionBackendFor(true) == DeletionBackend::NativeTrash);
#else
		CHECK(deletionBackendFor(true) == DeletionBackend::InternalJob);
#endif
	}

	SECTION("permanent deletion")
	{
#ifdef _WIN32
		CHECK(deletionBackendFor(false) == DeletionBackend::NativeShellPermanent);
#else
		CHECK(deletionBackendFor(false) == DeletionBackend::InternalJob);
#endif
	}
}
