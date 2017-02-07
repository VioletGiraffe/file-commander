#pragma once

#include <atomic>
#include <functional>
#include <thread>

class QFile;

class CFileComparator
{
public:
	enum ComparisonResult { Equal, NotEqual, Aborted };

	CFileComparator();

	void compareFilesThreaded(QFile& fileA, QFile& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback);
	void abortComparison();

private:
	void compareFiles(QFile& fileA, QFile& fileB, std::function<void (int)> progressCallback, std::function<void (ComparisonResult)> resultCallback);

	std::atomic<bool> _terminate {false};
	std::thread _comparisonThread;
};
