#pragma once

#include "centrypath.h"

#include "filesystem_error.hpp" // thin_io
#include "fs.hpp" // thin_io: timestamp

#include <optional>
#include <stdint.h>

// --- Structured errors ---

enum class FileErrorCategory
{
	NotFound,
	AlreadyExists,
	CrossDevice,
	ReadOnly, // Only from an unambiguous native read-only meaning; generic access denied stays PermissionDenied
	PermissionDenied,
	NotEnoughSpace,
	Unsupported,
	IoFailure
};

using NativeErrorCode = thin_io::filesystem_error_code;

struct CFileSystemError
{
	FileErrorCategory category;
	NativeErrorCode nativeCode; // 0 when the failure has no native call behind it
	QString diagnostic;
};

// --- Diagnostic-only failure identity ---
// Selects message text and logging context. Never a decision-policy or remembered-decision key.

enum class FailedAction
{
	InspectSource,
	ReadSource,
	CreateDestinationDirectory,
	PrepareStagingFile,
	WriteDestination,
	PreserveFileMetadata,
	PublishDestination,
	RenameEntry,
	MakeWritable,
	RemoveEntry,
	RemovePublishedMoveSource,
	CleanupStaging,
	PreserveDirectoryTimestamps
};

struct FailureDetails
{
	FailedAction action;
	CFileSystemError filesystemError;
};

// --- Entry inspection ---

enum class OperationEntryKind : uint8_t
{
	RegularFile,
	Directory,
	FileLink, // Link entry whose target is a regular file, is broken, or cannot be inspected
	DirectoryLink,
	Other // FIFO/socket/device etc., or a link to one - never streamed as a regular file
};

struct EntrySnapshot
{
	CEntryPath path;
	OperationEntryKind kind;
	uint64_t size = 0; // Followed-target size for FileLink; 0 for non-file kinds
};

// Unknown: a filesystem that does not expose stable identity - never assumed equal.
enum class SameEntryVerdict : uint8_t
{
	Same,
	Different,
	Unknown
};

// --- Mutation parameters and results ---

enum class ReplacementMode
{
	RequireAbsent,
	ReplaceExistingFile
};

enum class DirectoryCreationOutcome
{
	CreatedFinalDirectory, // The operation created the final directory, whether or not missing parents were created too
	FinalDirectoryAlreadyExisted // A directory (or directory link) is already present; never operation ownership
};

// What the product transfers to an operation-created destination directory: last-write time, and creation
// time only where the platform can set it. Access time is deliberately absent.
struct CopyableDirectoryTimes
{
	std::optional<thin_io::timestamp> creation;
	thin_io::timestamp lastWrite;
};
