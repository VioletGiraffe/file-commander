#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "threading/cthreadpool.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stddef.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Checks on worker threads whether folders exist, for a UI that polls the results.
// Each volume has its own thread, so the checks on one volume run one at a time: a spinning disk is not made to seek between them.
// Checking an unreachable network path can block for tens of seconds: destruction leaves running checks behind without waiting.
// Paths are absolute with '/' separators, as CFileSystemObject and resolveUserPath() produce them.
class CFolderAvailabilityChecker
{
public:
	struct Result
	{
		QString path;
		bool available;
	};

	// volumeRoots: the roots of the volumes known, such as from CController::volumes()
	explicit CFolderAvailabilityChecker(const std::vector<QString>& volumeRoots);

	// Starts a check of `absolutePath`, unless one was started for it already
	void check(const QString& absolutePath);
	// The checks finished since the last call
	[[nodiscard]] std::vector<Result> takeFinished();
	// True while takeFinished has not yet returned every check started
	[[nodiscard]] bool hasUnreportedChecks() const noexcept;

private:
	// The longest volume root `absolutePath` starts with; off the known volumes, its root, such as a network share's "//server/share"
	[[nodiscard]] QString volumeOf(const QString& absolutePath) const;

private:
	struct FinishedChecks
	{
		std::mutex mutex;
		std::vector<std::pair<std::u16string, bool>> results; // Each path and whether it is available
	};

	std::vector<QString> _volumeRoots; // Each ending with '/'
	// Co-owned by the running checks, which may outlive this object
	const std::shared_ptr<FinishedChecks> _finished = std::make_shared<FinishedChecks>();
	std::set<QString> _checkedPaths;
	size_t _reportedCount = 0;
	std::map<QString, std::unique_ptr<CThreadPool>> _threadByVolume; // One-thread pools: see the class comment
};
