#include "folderavailabilitychecker.h"


// Submodule includes
#include "paths/pathsyntax.h"


#include <exception>
#include <filesystem>
#include <system_error>

CFolderAvailabilityChecker::CFolderAvailabilityChecker(const std::vector<QString>& volumeRoots)
{
	_volumeRoots.reserve(volumeRoots.size());
	for (const QString& root : volumeRoots)
		_volumeRoots.push_back(withTrailingSeparator(root));
}

void CFolderAvailabilityChecker::check(const QString& absolutePath)
{
	if (!_checkedPaths.insert(absolutePath).second)
		return;

	std::unique_ptr<CThreadPool>& thread = _threadByVolume[volumeOf(absolutePath)];
	if (!thread)
		thread = std::make_unique<CThreadPool>(1, "Folder availability", CThreadPool::ShutdownPolicy::AbandonRunningTasks);

	// Standard library types only: an abandoned check may still be running while the application exits
	thread->enqueue([finished = _finished, path = absolutePath.toStdU16String()] {
		bool available = false;
		try
		{
			std::error_code error;
			available = std::filesystem::is_directory(std::filesystem::path{ path }, error);
		}
		catch (const std::exception&)
		{
			available = false; // The path does not convert to the platform's encoding. Still reported: takeFinished() counts every check.
		}

		std::lock_guard lock{ finished->mutex };
		finished->results.emplace_back(path, available);
	});
}

std::vector<CFolderAvailabilityChecker::Result> CFolderAvailabilityChecker::takeFinished()
{
	std::vector<std::pair<std::u16string, bool>> finished;
	{
		std::lock_guard lock{ _finished->mutex };
		finished = std::exchange(_finished->results, {});
	}
	_reportedCount += finished.size();

	std::vector<Result> results;
	results.reserve(finished.size());
	for (const auto& [path, available] : finished)
		results.push_back({ QString::fromStdU16String(path), available });
	return results;
}

bool CFolderAvailabilityChecker::hasUnreportedChecks() const noexcept
{
	return _reportedCount < _checkedPaths.size();
}

QString CFolderAvailabilityChecker::volumeOf(const QString& absolutePath) const
{
	const QString path = withTrailingSeparator(absolutePath);

	// Letter case is ignored: a wrong match only puts two volumes' checks on one thread
	const QString* longestRoot = nullptr;
	for (const QString& root : _volumeRoots)
	{
		if (path.startsWith(root, Qt::CaseInsensitive) && (!longestRoot || root.size() > longestRoot->size()))
			longestRoot = &root;
	}
	return longestRoot ? *longestRoot : path.left(absoluteRootLength(path));
}
