#pragma once

#include "fileoperationtypes.h"

#include <expected>

// Category of a captured native error code. Context-free; primitives that know more (e.g. rename's
// destination-exists forms) refine the result locally before returning it.
[[nodiscard]] FileErrorCategory classifyNativeError(NativeErrorCode code) noexcept;

// Complete structured error from a captured native code: classification plus the OS's own description.
[[nodiscard]] CFileSystemError makeFileSystemError(NativeErrorCode code);

// True for entries that are links in the operation sense: POSIX symlinks, and on Windows only name-surrogate
// reparse points (symlinks, junctions) - other reparse entries (OneDrive placeholders and the like) are
// ordinary files/directories.
[[nodiscard]] bool isLinkEntry(const thin_io::entry_attributes& attributes) noexcept;

// Fresh state of one filesystem entry; the canonical mapping to OperationEntryKind. nullopt = no entry at
// the path. Link kind precedes followed classification: a broken (or uninspectable) link is an existing
// FileLink entry; a link to a non-regular non-directory target is Other.
[[nodiscard]] std::expected<std::optional<EntrySnapshot>, CFileSystemError> inspectEntry(const CEntryPath& path);

// Fresh filesystem identity for same-object decisions and link-cycle detection.
// nullopt = this filesystem does not expose a stable identity.
[[nodiscard]] std::expected<std::optional<thin_io::entry_identity>, CFileSystemError> readEntryIdentity(const CEntryPath& path, thin_io::link_behavior linkBehavior);

// Same-object check over fresh identities; Unknown when either side's identity is unavailable.
// An entry absent on either side is Different.
[[nodiscard]] std::expected<SameEntryVerdict, CFileSystemError> checkSameEntry(const CEntryPath& a, const CEntryPath& b, thin_io::link_behavior linkBehavior);

// Fresh preflight/reactive writability of a non-link regular file, answered from the entry itself.
// A link or non-file entry at the path is rejected with Unsupported - the answer never comes from a link target.
[[nodiscard]] std::expected<bool, CFileSystemError> isEntryWritableNoFollow(const EntrySnapshot& entry);

// Directory times for later application to an operation-created destination directory. Follows a directory
// link deliberately: its only link use is materialization of the target directory's contents.
[[nodiscard]] std::expected<CopyableDirectoryTimes, CFileSystemError> readCopyableDirectoryTimes(const CEntryPath& source);

// Stateless native mutations. These own native path conversion, link-entry addressing, and platform branches;
// callers never learn which platform primitive ran.
class CFileSystemMutator
{
public:
	// Atomic rename. RequireAbsent uses the native exclusive mechanism, never check-then-rename; where the
	// filesystem reports that mechanism itself unsupported, degrades internally to a fresh no-follow recheck
	// plus plain rename and returns AlreadyExists if an entry is present. ReplaceExistingFile atomically
	// replaces a regular-file (or link) destination; a directory destination is rejected.
	static std::expected<void, CFileSystemError> renameEntry(const CEntryPath& source, const CEntryPath& destination, ReplacementMode replacement);

	// Removes the entry itself: links are unlinked, never followed; a directory must be empty.
	static std::expected<void, CFileSystemError> removeEntry(const EntrySnapshot& entry);

	// Creates the directory and any missing parents. FinalDirectoryAlreadyExisted (a directory or directory
	// link already present) returns to destination resolution; any other entry at the path is an AlreadyExists error.
	static std::expected<DirectoryCreationOutcome, CFileSystemError> createDirectories(const CEntryPath& path);

	static std::expected<void, CFileSystemError> applyDirectoryTimes(const CEntryPath& destination, const CopyableDirectoryTimes& times);

	// Sets or clears write permission on a non-link regular file; never follows or remediates a link.
	static std::expected<void, CFileSystemError> setEntryWritable(const EntrySnapshot& entry, bool writable);
};
