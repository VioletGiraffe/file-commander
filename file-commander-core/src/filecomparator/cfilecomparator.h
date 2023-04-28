#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class QIODevice;

class CFileComparator
{
public:
	enum ComparisonResult { Equal, NotEqual, Aborted };

	CFileComparator() noexcept = default;
	~CFileComparator();

	CFileComparator(const CFileComparator&) = delete;
	CFileComparator& operator=(const CFileComparator&) = delete;

	void compareFilesThreaded(std::unique_ptr<QIODevice>&& fileA, std::unique_ptr<QIODevice>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback);
	void compareFiles(QIODevice& fileA, QIODevice& fileB, const std::function<void(int)>& progressCallback, const std::function<void(ComparisonResult)>& resultCallback);
	void abortComparison();

private:
	std::atomic<bool> _terminate {false};
	std::thread _comparisonThread;
};
