#include "cfilecomparator.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "math/math.hpp"
#include "threading/thread_helpers.h"
#include "utility/on_scope_exit.hpp"

DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

#include <string.h>
#include <memory>
#include <utility> // std::move

CFileComparator::~CFileComparator() noexcept
{
	abortComparison();
}

void CFileComparator::compareFilesThreaded(std::unique_ptr<QFile>&& fileA, std::unique_ptr<QFile>&& fileB, const std::function<void (int)>& progressCallback, const std::function<void (ComparisonResult)>& resultCallback)
{
	if (_comparisonThread.joinable())
		_comparisonThread.join();

	_terminate = false;
	_comparisonThread = std::thread([fileA{ std::move(fileA) }, fileB{std::move(fileB)}, progressCallback, resultCallback, this]() {
		setThreadName("CFileComparator thread");
		compareFiles(*fileA, *fileB, progressCallback, resultCallback);
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

void CFileComparator::compareFiles(QFile& fileA, QFile& fileB, const std::function<void(int)>& progressCallback, const std::function<void(ComparisonResult)>& resultCallback)
{
	assert_debug_only(fileA.isOpen() && fileB.isOpen());
	assert_debug_only(progressCallback);
	assert_debug_only(resultCallback);

	EXEC_ON_SCOPE_EXIT([&progressCallback]() {progressCallback(100);});

	if (fileA.size() != fileB.size())
	{
		resultCallback(NotEqual);
		return;
	}

	static constexpr qint64 blockSize = 1LL * 1024LL * 1024LL; // 1 MiB block size

	for (qint64 pos = 0, size = fileA.size(); pos < size && !_terminate; pos += blockSize)
	{
		const qint64 sizeToCompare = std::min(blockSize, size - pos);

		auto* aData = fileA.map(pos, sizeToCompare);
		auto* bData = fileB.map(pos, sizeToCompare);

		if (!aData || !bData) [[unlikely]]
		{
			resultCallback(Aborted);
			return;
		}

		const bool equal = ::memcmp(aData, bData, static_cast<uint64_t>(sizeToCompare)) == 0;

		fileA.unmap(aData);
		fileB.unmap(bData);

		if (!equal)
		{
			resultCallback(NotEqual);
			return;
		}

		progressCallback(Math::round<int>(pos * 100 / size));
	}

	resultCallback(!_terminate ? Equal : Aborted);
}
