#pragma once

#include "fileoperationtypes.h"

#include "file.hpp" // thin_io

#include <expected>
#include <optional>

// One staged copy of one regular file, or of a materialized file link's target: an exclusively created,
// hidden temporary sibling of the destination receives the data and the required source metadata, then
// atomically becomes the destination entry. Move-only; a session that was neither committed nor aborted
// cleans up best-effort in the destructor. Reports FailureDetails only - the executor owns paths in
// diagnostics, prompts, retry, progress, and the durability choice.
class CStagedFileCopy
{
public:
	// Opens the source (following a link - the opened target then supplies both bytes and metadata),
	// captures required metadata from that handle, exclusively creates the staging sibling, fixes its
	// logical size, and best-effort preallocates. On failure, no filesystem state is left behind.
	[[nodiscard]] static std::expected<CStagedFileCopy, FailureDetails> begin(CEntryPath source, CEntryPath destination);

	CStagedFileCopy(CStagedFileCopy&& other) noexcept;
	CStagedFileCopy(const CStagedFileCopy&) = delete;
	CStagedFileCopy& operator=(const CStagedFileCopy&) = delete;
	CStagedFileCopy& operator=(CStagedFileCopy&&) = delete;
	~CStagedFileCopy();

	// Transfers at most maxBytes more source bytes into the staging file; see CopyChunkResult.
	[[nodiscard]] std::expected<CopyChunkResult, FailureDetails> writeNext(uint64_t maxBytes);

	// Flushes per the durability policy, applies the captured metadata through the staging handle, closes
	// it, and publishes the staging file as the destination entry. The publishing rename is the last
	// action, so the only two outcomes are failure-before-publication and successful publication.
	[[nodiscard]] std::expected<void, FailureDetails> commit(ReplacementMode replacement, CommitDurability durability);

	// Removes the staging data of a not-yet-published session; reports what could not be cleaned up.
	// One-shot: after abort(), the destructor does not retry.
	[[nodiscard]] std::expected<void, FailureDetails> abort();

private:
	CStagedFileCopy(CEntryPath destination, CEntryPath stagingPath, thin_io::file sourceFile, thin_io::file stagingFile,
					const thin_io::entry_times& sourceTimes, thin_io::file_permissions sourcePermissions, uint64_t sourceSize) noexcept;

	[[nodiscard]] std::optional<NativeErrorCode> removeStagingFile();

	enum class State
	{
		Transferring,
		ReadyToCommit,
		Committed,
		Aborted,
		MovedFrom
	};

	CEntryPath _destinationPath;
	CEntryPath _stagingPath;
	thin_io::file _sourceFile;
	thin_io::file _stagingFile;
	thin_io::entry_times _sourceTimes; // Access time already cleared: it is never transferred
	thin_io::file_permissions _sourcePermissions;
	uint64_t _sourceSize = 0;
	uint64_t _bytesTransferred = 0;
	State _state = State::Transferring;
};
