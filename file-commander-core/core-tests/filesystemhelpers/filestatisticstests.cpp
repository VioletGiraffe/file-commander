#include "filesystemhelpers/filestatistics.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "threading/cthreadpool.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <chrono>
#include <future>
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

	CThreadPool pool{ 2, "Statistics test pool" };
	std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
	const FileStatistics stats = calculateStatsFor({ dir.path() }, pool, false, {}, &occupiedSpaceSoFar);
	CHECK(stats.occupiedSpace == 410);
	CHECK(occupiedSpaceSoFar == stats.occupiedSpace);
}

TEST_CASE("calculateStatsFor completes when called from a task of its own pool while every worker is busy", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	// Declared before the pool: after a failed wait below, destroying the pool waits for the task that writes stats
	FileStatistics stats;
	// The task occupies the pool's only worker, so the calling thread must scan alone
	CThreadPool pool{ 1, "Statistics test pool" };
	auto scanned = pool.enqueueWithFuture([&] { stats = calculateStatsFor({ dir.path() }, pool); });
	REQUIRE(scanned.wait_for(10s) == std::future_status::ready);
	CHECK(stats.files == 3);
	CHECK(stats.occupiedSpace == 410);
}

TEST_CASE("CFileStatisticsScan finds what calculateStatsFor finds", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	CThreadPool pool{ 2, "Statistics test pool" };
	CFileStatisticsScan scan{ { dir.path() }, pool };
	REQUIRE(finishesInTime(scan));
	REQUIRE(scan.result().has_value());

	const FileStatistics& stats = scan.result()->stats;
	CHECK(stats.files == 3);
	CHECK(stats.folders == 3); // The root counts
	CHECK(stats.occupiedSpace == 410);
	CHECK(stats == calculateStatsFor({ dir.path() }, pool));
}

TEST_CASE("CFileStatisticsScan's current folder is one it scanned", "[filestatistics]")
{
	const QTemporaryDir dir;
	REQUIRE(dir.isValid());
	createTree(dir.path());

	CThreadPool pool{ 2, "Statistics test pool" };
	CFileStatisticsScan scan{ { dir.path() }, pool };
	REQUIRE(finishesInTime(scan));

	const QString current = QFileInfo{ scan.currentFolder() }.canonicalFilePath();
	const QDir root{ dir.path() };
	CHECK((current == QFileInfo{ root.path() }.canonicalFilePath() || current == QFileInfo{ root.filePath(QStringLiteral("sub")) }.canonicalFilePath()
		|| current == QFileInfo{ root.filePath(QStringLiteral("sub/deeper")) }.canonicalFilePath()));
}
