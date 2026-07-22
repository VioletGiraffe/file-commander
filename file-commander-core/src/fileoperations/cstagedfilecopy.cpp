#include "cstagedfilecopy.h"
#include "cfilesystemmutator.h"
#include "operationtesthooks.h"
#include "thiniobridge.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QUuid>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#endif

#include <algorithm>

using OperationTestHooks::fireHook;
using OperationTestHooks::Point;

namespace
{

std::unexpected<FailureDetails> fail(const FailedAction action, const NativeErrorCode code)
{
	return std::unexpected{ FailureDetails{ action, makeFileSystemError(code) } };
}

// Preallocation is an optimization, so a filesystem that cannot service it must not fail the copy.
// The arguments are validated by construction, which is what makes the invalid-argument code here mean
// "cannot service the request" rather than a real argument error.
bool isUnsupportedPreallocationError(const NativeErrorCode code) noexcept
{
#ifdef _WIN32
	return classifyNativeError(code) == FileErrorCategory::Unsupported || code == ERROR_INVALID_PARAMETER;
#else
	return classifyNativeError(code) == FileErrorCategory::Unsupported || code == EINVAL;
#endif
}

} // namespace

std::expected<CStagedFileCopy, FailureDetails> CStagedFileCopy::begin(CEntryPath source, CEntryPath destination)
{
	assert_debug_only(!destination.isRoot());

	thin_io::file sourceFile;
	{
		const auto sourceNative = thinIoPath(source);
		if (!sourceFile.open(nativeCStr(sourceNative), thin_io::file::access_mode::Read)) [[unlikely]]
			return fail(FailedAction::ReadSource, captureNativeError());
	}

	// Metadata comes from the open handle - provably the same effective file that supplies the bytes,
	// even when the source path is a link being materialized.
	thin_io::entry_times sourceTimes;
	thin_io::file_permissions sourcePermissions;
	if (const auto forcedError = fireHook(Point::StagedCopy_CaptureMetadata_Native))
		return fail(FailedAction::PreserveFileMetadata, *forcedError);

	if (const auto times = sourceFile.times(); times) [[likely]]
		sourceTimes = *times;
	else
		return fail(FailedAction::PreserveFileMetadata, captureNativeError());
	sourceTimes.last_access.reset(); // Never transferred: reading the source for this copy already changed it

	if (const auto permissions = sourceFile.permissions(); permissions) [[likely]]
		sourcePermissions = *permissions;
	else
		return fail(FailedAction::PreserveFileMetadata, captureNativeError());

	uint64_t sourceSize = 0;
	if (const auto size = sourceFile.size(); size) [[likely]]
		sourceSize = *size;
	else
		return fail(FailedAction::ReadSource, captureNativeError());

	// The exclusive create is the collision check; a (vanishingly unlikely) name collision just means
	// another attempt with a fresh unique name.
	std::optional<CEntryPath> stagingPath;
	thin_io::file stagingFile;
	static constexpr int maxCreationAttempts = 10;
	for (int attempt = 0; attempt < maxCreationAttempts; ++attempt)
	{
		CEntryPath candidate = destination.parent().child(
			QStringLiteral(".file-commander-copy-") % QUuid::createUuid().toString(QUuid::WithoutBraces) % QStringLiteral(".tmp"));

		NativeErrorCode errorCode;
		if (const auto forcedError = fireHook(Point::StagedCopy_CreateStaging_Native))
			errorCode = *forcedError;
		else
		{
			const auto candidateNative = thinIoPath(candidate);
			if (stagingFile.open(nativeCStr(candidateNative), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew))
			{
				stagingPath = mv(candidate);
				break;
			}
			errorCode = captureNativeError();
		}

		if (classifyNativeError(errorCode) != FileErrorCategory::AlreadyExists)
			return fail(FailedAction::PrepareStagingFile, errorCode);
	}
	if (!stagingPath) [[unlikely]]
		return std::unexpected{ FailureDetails{ FailedAction::PrepareStagingFile,
			CFileSystemError{ FileErrorCategory::AlreadyExists, 0, QStringLiteral("Could not create a unique temporary file next to the destination") } } };

#ifdef _WIN32
	// Cosmetic, so best-effort: hides the in-progress temporary from destination listings. POSIX gets the
	// same from the dot-prefixed name. commit() settles the final state by applying the source permissions.
	(void)stagingFile.set_hidden(true);
#endif

	const auto failAndDiscardStaging = [&](const FailedAction action, const NativeErrorCode code) {
		(void)stagingFile.close();
		const auto stagingNative = thinIoPath(*stagingPath);
		(void)thin_io::file::delete_file(nativeCStr(stagingNative));
		return fail(action, code);
	};

	// The final logical size first, then best-effort physical reservation, so storage exhaustion surfaces
	// here rather than mid-transfer and the file is less likely to fragment.
	if (const auto forcedError = fireHook(Point::StagedCopy_ResizeStaging_Native))
		return failAndDiscardStaging(FailedAction::PrepareStagingFile, *forcedError);
	if (!stagingFile.resize(sourceSize)) [[unlikely]]
		return failAndDiscardStaging(FailedAction::PrepareStagingFile, captureNativeError());

	NativeErrorCode preallocationError{};
	bool preallocationFailed = false;
	if (const auto forcedError = fireHook(Point::StagedCopy_PreallocateStaging_Native))
	{
		preallocationError = *forcedError;
		preallocationFailed = true;
	}
	else if (!stagingFile.preallocate(sourceSize))
	{
		preallocationError = captureNativeError();
		preallocationFailed = true;
	}
	if (preallocationFailed && !isUnsupportedPreallocationError(preallocationError)) [[unlikely]]
		return failAndDiscardStaging(FailedAction::PrepareStagingFile, preallocationError);

	return CStagedFileCopy{ mv(destination), mv(*stagingPath), mv(sourceFile), mv(stagingFile), sourceTimes, sourcePermissions, sourceSize };
}

CStagedFileCopy::CStagedFileCopy(CEntryPath destination, CEntryPath stagingPath, thin_io::file sourceFile, thin_io::file stagingFile,
								 const thin_io::entry_times& sourceTimes, const thin_io::file_permissions sourcePermissions, const uint64_t sourceSize) noexcept
	: _destinationPath{ mv(destination) }
	, _stagingPath{ mv(stagingPath) }
	, _sourceFile{ mv(sourceFile) }
	, _stagingFile{ mv(stagingFile) }
	, _sourceTimes{ sourceTimes }
	, _sourcePermissions{ sourcePermissions }
	, _sourceSize{ sourceSize }
{
}

CStagedFileCopy::CStagedFileCopy(CStagedFileCopy&& other) noexcept
	: _destinationPath{ mv(other._destinationPath) }
	, _stagingPath{ mv(other._stagingPath) }
	, _sourceFile{ mv(other._sourceFile) }
	, _stagingFile{ mv(other._stagingFile) }
	, _sourceTimes{ other._sourceTimes }
	, _sourcePermissions{ other._sourcePermissions }
	, _sourceSize{ other._sourceSize }
	, _bytesTransferred{ other._bytesTransferred }
	, _state{ other._state }
{
	other._state = State::MovedFrom;
}

CStagedFileCopy::~CStagedFileCopy()
{
	// Safety net only; the executor is expected to commit or abort explicitly.
	if (_state == State::Transferring || _state == State::ReadyToCommit)
		(void)abort();
}

std::expected<CopyChunkResult, FailureDetails> CStagedFileCopy::writeNext(const uint64_t maxBytes)
{
	assert_debug_only(_state == State::Transferring);
	assert_debug_only(maxBytes > 0);

	const uint64_t remaining = _sourceSize - _bytesTransferred;
	if (remaining == 0)
	{
		_state = State::ReadyToCommit;
		return CopyChunkResult{ 0, true };
	}

	const uint64_t chunkSize = std::min(maxBytes, remaining);
	void* const chunk = _sourceFile.mmap(thin_io::file::mmap_access_mode::ReadOnly, _bytesTransferred, chunkSize);
	if (chunk == nullptr) [[unlikely]]
		return fail(FailedAction::ReadSource, captureNativeError());

	std::optional<uint64_t> written;
	NativeErrorCode writeErrorCode{};
	if (const auto forcedError = fireHook(Point::StagedCopy_WriteStaging_Native))
		writeErrorCode = *forcedError;
	else
	{
		written = _stagingFile.write(chunk, chunkSize);
		if (!written) [[unlikely]]
			writeErrorCode = captureNativeError();
	}

	[[maybe_unused]] const bool unmapped = _sourceFile.unmap(chunk);
	assert_debug_only(unmapped);

	if (!written) [[unlikely]]
		return fail(FailedAction::WriteDestination, writeErrorCode);
	if (*written == 0) [[unlikely]] // A zero-byte success for a non-empty chunk would stall the executor's loop forever
		return std::unexpected{ FailureDetails{ FailedAction::WriteDestination,
			CFileSystemError{ FileErrorCategory::IoFailure, 0, QStringLiteral("Zero bytes written to the staging file") } } };

	_bytesTransferred += *written;
	if (_bytesTransferred == _sourceSize)
	{
		_state = State::ReadyToCommit;
		return CopyChunkResult{ *written, true };
	}
	return CopyChunkResult{ *written, false };
}

std::expected<void, FailureDetails> CStagedFileCopy::commit(const ReplacementMode replacement, const CommitDurability durability)
{
	assert_debug_only(_state == State::ReadyToCommit);

	if (durability == CommitDurability::FlushBeforePublish)
	{
		if (const auto forcedError = fireHook(Point::StagedCopy_FlushStaging_Native))
			return fail(FailedAction::WriteDestination, *forcedError);
		if (!_stagingFile.fdatasync()) [[unlikely]]
			return fail(FailedAction::WriteDestination, captureNativeError());
	}

	if (const auto forcedError = fireHook(Point::StagedCopy_ApplyMetadata_Native))
		return fail(FailedAction::PreserveFileMetadata, *forcedError);
	if (!_stagingFile.set_times(_sourceTimes)) [[unlikely]]
		return fail(FailedAction::PreserveFileMetadata, captureNativeError());
	// On Windows this also settles the staging file's temporary hidden attribute to the source's state.
	if (!_stagingFile.set_permissions(_sourcePermissions)) [[unlikely]]
		return fail(FailedAction::PreserveFileMetadata, captureNativeError());

	(void)_sourceFile.close(); // A read-side close failure puts no data at risk

	if (const auto forcedError = fireHook(Point::StagedCopy_CloseStaging_Native))
		return fail(FailedAction::WriteDestination, *forcedError);
	if (!_stagingFile.close()) [[unlikely]]
		return fail(FailedAction::WriteDestination, captureNativeError());

	if (auto published = CFileSystemMutator::renameEntry(_stagingPath, _destinationPath, replacement); !published) [[unlikely]]
		return std::unexpected{ FailureDetails{ FailedAction::PublishDestination, mv(published.error()) } };

	_state = State::Committed;
	return {};
}

std::expected<void, FailureDetails> CStagedFileCopy::abort()
{
	assert_debug_only(_state == State::Transferring || _state == State::ReadyToCommit);
	_state = State::Aborted; // One-shot even on failure: the destructor must not retry a reported cleanup

	(void)_sourceFile.close(); // A read-side close failure puts no data at risk

	std::optional<NativeErrorCode> cleanupErrorCode;
	if (_stagingFile.is_open() && !_stagingFile.close()) [[unlikely]]
		cleanupErrorCode = captureNativeError();

	if (const auto removalErrorCode = removeStagingFile()) [[unlikely]]
		cleanupErrorCode = *removalErrorCode; // Staging data left behind outweighs a close failure in the report

	if (cleanupErrorCode) [[unlikely]]
		return fail(FailedAction::CleanupStaging, *cleanupErrorCode);
	return {};
}

// nullopt on success. commit() may already have made the staging file read-only (copied from a read-only
// source), which fails Windows deletion with PermissionDenied - remediated and retried once.
std::optional<NativeErrorCode> CStagedFileCopy::removeStagingFile()
{
	const auto stagingNative = thinIoPath(_stagingPath);

	NativeErrorCode errorCode;
	if (const auto forcedError = fireHook(Point::StagedCopy_RemoveStaging_Native))
		errorCode = *forcedError;
	else if (thin_io::file::delete_file(nativeCStr(stagingNative)))
		return {};
	else
		errorCode = captureNativeError();

	if (classifyNativeError(errorCode) == FileErrorCategory::PermissionDenied)
	{
		const EntrySnapshot stagingEntry{ _stagingPath, OperationEntryKind::RegularFile, 0 };
		if (CFileSystemMutator::setEntryWritable(stagingEntry, true).has_value() && thin_io::file::delete_file(nativeCStr(stagingNative)))
			return {};
	}

	return errorCode; // The original failure explains the cleanup problem better than a retry side effect would
}
