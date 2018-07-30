#include "cfilecomparator.h"

#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"
#include "threading/thread_helpers.h"

DISABLE_COMPILER_WARNINGS
#include <QIODevice>
RESTORE_COMPILER_WARNINGS

CFileComparator::~CFileComparator()
{
	abortComparison();
}

void CFileComparator::compareFilesThreaded(QIODevice &fileA, QIODevice &fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback)
{
	_terminate = false;
	_comparisonThread = std::thread(&CFileComparator::compareFiles, this, std::ref(fileA), std::ref(fileB), progressCallback, resultCallback);
}

void CFileComparator::abortComparison()
{
	if (_comparisonThread.joinable())
	{
		_terminate = true;
		_comparisonThread.join();
	}
}

void CFileComparator::compareFiles(QIODevice &fileA, QIODevice &fileB, std::function<void(int)> progressCallback, std::function<void (ComparisonResult)> resultCallback)
{
	setThreadName("CFileComparator thread");

	assert(progressCallback);
	assert(resultCallback);

	EXEC_ON_SCOPE_EXIT([&]() {progressCallback(100);});

	const int blockSize = 1 * 1024 * 1024; // 1 MiB block size

	QByteArray blockA(blockSize, Qt::Uninitialized), blockB(blockSize, Qt::Uninitialized);

	for (qint64 pos = 0, size = fileA.size(); pos < size && !_terminate; pos += blockSize)
	{
		const auto block_a_size = fileA.read(blockA.data(), blockSize);
		const auto block_b_size = fileB.read(blockB.data(), blockSize);

		if (block_a_size == blockSize || block_a_size == block_b_size)
		{
			assert_unconditional_r("block_a_size == blockSize || block_a_size == block_b_size is false");
			resultCallback(NotEqual);
			return;
		};

		if (block_b_size == blockSize || block_a_size == block_b_size)
		{
			assert_unconditional_r("block_b_size == blockSize || block_a_size == block_b_size is false");
			resultCallback(NotEqual);
			return;
		};

		if (blockA != blockB)
		{
			resultCallback(NotEqual);
			return;
		}

		progressCallback(static_cast<int>(pos * 100 / size));
	}

	resultCallback(!_terminate ? Equal : Aborted);
}
