#include "filesystemhelpers/filestatistics.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace {

void createFile(const QString& path, const qsizetype size)
{
	QFile file{ path };
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(QByteArray(size, 'x')) == size);
}

// root/a.bin, root/sub/b.bin, root/sub/deeper/c.txt
void createTree(const QString& root)
{
	REQUIRE(QDir{ root }.mkpath(QStringLiteral("sub/deeper")));
	createFile(root + QStringLiteral("/a.bin"), 100);
	createFile(root + QStringLiteral("/sub/b.bin"), 300);
	createFile(root + QStringLiteral("/sub/deeper/c.txt"), 10);
}

// Polls the scan as the UI does
[[nodiscard]] bool finishesInTime(CFileStatisticsScan& scan)
{
	const auto deadline = std::chrono::steady_clock::now() + 10s;
	while (!scan.finished())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::sleep_for(1ms);
	}
	return true;
}

}

TEST_CASE("calculateStatsFor's running occupied space ends at the total", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
	const FileStatistics stats = calculateStatsFor({ dir.path() }, false, {}, &occupiedSpaceSoFar);
	CHECK(stats.occupiedSpace == 410);
	CHECK(occupiedSpaceSoFar == stats.occupiedSpace);
}

TEST_CASE("CFileStatisticsScan finds what calculateStatsFor finds", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	CFileStatisticsScan scan{ { dir.path() } };
	REQUIRE(finishesInTime(scan));
	REQUIRE(scan.result().has_value());

	const FileStatistics& stats = scan.result()->stats;
	CHECK(stats.files == 3);
	CHECK(stats.folders == 3); // The root counts
	CHECK(stats.occupiedSpace == 410);
	CHECK(stats == calculateStatsFor({ dir.path() }));
}

TEST_CASE("CFileStatisticsScan's current folder is one it scanned", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	CFileStatisticsScan scan{ { dir.path() } };
	REQUIRE(finishesInTime(scan));

	const QString current = QFileInfo{ scan.currentFolder() }.canonicalFilePath();
	const QDir root{ dir.path() };
	CHECK((current == QFileInfo{ root.path() }.canonicalFilePath() || current == QFileInfo{ root.filePath(QStringLiteral("sub")) }.canonicalFilePath()
		|| current == QFileInfo{ root.filePath(QStringLiteral("sub/deeper")) }.canonicalFilePath()));
}
