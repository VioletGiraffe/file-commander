#include "cfilecomparator.h"

#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"
#include "threading/thread_helpers.h"
#include "math/math.hpp"

DISABLE_COMPILER_WARNINGS
#include <QIODevice>
RESTORE_COMPILER_WARNINGS

#include <cstring>
#include <memory>
#include <utility> // std::move

CFileComparator::~CFileComparator()
{
	abortComparison();
}

void CFileComparator::compareFilesThreaded(std::unique_ptr<QIODevice>&& fileA, std::unique_ptr<QIODevice>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback)
{
	if (_comparisonThread.joinable())
		_comparisonThread.join();

	_terminate = false;
	_comparisonThread = std::thread([fileA{ std::move(fileA) }, fileB{std::move(fileB)}, progressCallback, resultCallback, this]() mutable {
		setThreadName("CFileComparator thread");
		compareFiles(std::move(fileA), std::move(fileB), progressCallback, resultCallback);
	});
}

void CFileComparator::abortComparison()
{
	if (_comparisonThread.joinable())
	{
		_terminate = true;
		_comparisonThread.join();
	}
}

void CFileComparator::compareFiles(std::unique_ptr<QIODevice>&& fileA, std::unique_ptr<QIODevice>&& fileB, const std::function<void(int)>& progressCallback, const std::function<void(ComparisonResult)>& resultCallback)
{
	assert(fileA->isOpen() && fileB->isOpen());
	assert(progressCallback);
	assert(resultCallback);

	EXEC_ON_SCOPE_EXIT([&]() {progressCallback(100);});

	if (fileA->size() != fileB->size())
	{
		resultCallback(NotEqual);
		return;
	}

	constexpr qint64 blockSize = 1 * 1024 * 1024; // 1 MiB block size

	const auto blockA = std::make_unique<char[]>(blockSize);
	const auto blockB = std::make_unique<char[]>(blockSize);

	for (qint64 pos = 0, size = fileA->size(); pos < size && !_terminate; pos += blockSize)
	{
		const auto block_a_size = fileA->read(blockA.get(), blockSize);
		const auto block_b_size = fileB->read(blockB.get(), blockSize);

		if (block_a_size != block_b_size || std::memcmp(blockA.get(), blockB.get(), block_a_size) != 0)
		{
			resultCallback(NotEqual);
			return;
		};

		progressCallback(Math::round<int>(pos * 100 / size));
	}

	assert_r(fileA->atEnd());
	assert_r(fileB->atEnd());

	resultCallback(!_terminate ? Equal : Aborted);
}
