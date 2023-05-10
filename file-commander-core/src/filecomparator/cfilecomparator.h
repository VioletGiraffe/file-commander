#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class QFile;

class CFileComparator
{
public:
	enum ComparisonResult { Equal, NotEqual, Aborted };

	CFileComparator() noexcept = default;
	~CFileComparator() noexcept;

	CFileComparator(const CFileComparator&) = delete;
	CFileComparator& operator=(const CFileComparator&) = delete;

	void compareFilesThreaded(std::unique_ptr<QFile>&& fileA, std::unique_ptr<QFile>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback);
	void compareFiles(QFile& fileA, QFile& fileB, const std::function<void(int)>& progressCallback, const std::function<void(ComparisonResult)>& resultCallback);
	void abortComparison();

private:
	std::atomic<bool> _terminate {false};
	std::thread _comparisonThread;
};
