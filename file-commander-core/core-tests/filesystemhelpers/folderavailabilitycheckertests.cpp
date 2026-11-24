#include "filesystemhelpers/folderavailabilitychecker.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QFile>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <map>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("CFolderAvailabilityChecker finds a folder, not a file or a missing path, and checks each path once", "[folderavailability]")
{
	const QTemporaryDir root;
	REQUIRE(root.isValid());
	const QString folder = root.path();
	const QString file = root.filePath(QStringLiteral("file.txt"));
	const QString missing = root.filePath(QStringLiteral("missing"));
	{
		QFile newFile{ file };
		REQUIRE(newFile.open(QFile::WriteOnly));
	}

	CFolderAvailabilityChecker checker{ { root.path() } }; // All on one volume, so on one thread
	for (const QString& path : { folder, file, missing, folder })
		checker.check(path);

	// Polled as the UI does
	std::map<QString, bool> results;
	size_t resultCount = 0;
	for (const auto deadline = std::chrono::steady_clock::now() + 10s; checker.hasUnreportedChecks() && std::chrono::steady_clock::now() < deadline;)
	{
		std::this_thread::sleep_for(10ms);
		for (const CFolderAvailabilityChecker::Result& result : checker.takeFinished())
		{
			results[result.path] = result.available;
			++resultCount;
		}
	}

	CHECK_FALSE(checker.hasUnreportedChecks());
	CHECK(resultCount == 3);
	CHECK(results[folder]);
	CHECK_FALSE(results[file]);
	CHECK_FALSE(results[missing]);
}
