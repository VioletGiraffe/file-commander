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

	~CFileComparator();

	void compareFilesThreaded(std::unique_ptr<QIODevice>&& fileA, std::unique_ptr<QIODevice>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback);
	void abortComparison();

private:
	void compareFiles(std::unique_ptr<QIODevice>&& fileA, std::unique_ptr<QIODevice>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback);

	std::atomic<bool> _terminate {false};
	std::thread _comparisonThread;
};
