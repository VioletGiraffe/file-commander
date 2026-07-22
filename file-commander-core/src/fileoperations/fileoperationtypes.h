#pragma once

#include "centrypath.h"

#include "filesystem_error.hpp" // thin_io
#include "fs.hpp" // thin_io: timestamp

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <variant>
#include <vector>

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
	InspectDestination,
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

// Whether staged data must reach storage before publication. The executor owns the choice: flushing is
// required exactly where publication destroys the only other copy of the data.
enum class CommitDurability
{
	NoFlush,           // Fresh copy: the source still exists, so a crash at worst means re-running the copy
	FlushBeforePublish // Move or authorized replacement: publication (then source removal) destroys the only other copy
};

// One writeNext() step. bytesWritten may be less than requested - partial writes are normal and the next
// call continues; readyToCommit reports that every source byte is staged.
struct CopyChunkResult
{
	uint64_t bytesWritten;
	bool readyToCommit;
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

// --- Typed requests ---
// The UI resolves its destination-text heuristics into an explicit intent exactly once, at the request
// boundary; no resolver, executor, or mutator ever reinterprets intent from source count, path spelling,
// or destination existence.

enum class DestinationIntent
{
	IntoDirectory, // Path D maps source root A to proposed target D/A
	ExactEntry     // Path X is the single source's exact proposed entry, whether or not it currently exists
};

struct DestinationSpec
{
	DestinationIntent intent;
	CEntryPath path;
};

enum class TransferKind
{
	Copy,
	Move
};

struct TransferRequest
{
	TransferKind kind;
	std::vector<CEntryPath> sources;
	DestinationSpec destination;
};

struct PermanentDeleteRequest
{
	std::vector<CEntryPath> sources;
};

using FileOperationRequest = std::variant<TransferRequest, PermanentDeleteRequest>;

// One selected source with its proposed destination - the representation destination resolution and
// execution consume. Every root has its own destination and can later select rename or traversal
// at a subtree boundary.
struct RootTransferIntent
{
	CEntryPath source;
	CEntryPath proposedDestination;
};

enum class RequestValidationError
{
	NoSources, // Also the result of a selection reduced to nothing by the synthetic-parent filter
	InvalidPath,
	RootSource, // A filesystem root is never a valid source: it cannot be named at a destination nor deleted
	ExactEntryRequiresSingleSource
};

// The only entry points for raw UI/drag-and-drop path text. Trim and parse every path, filter the
// synthetic parent ([..]) entries once, and enforce the source-count rules. The UI has already chosen
// the intent; no filesystem inspection happens here.
[[nodiscard]] std::expected<TransferRequest, RequestValidationError> makeTransferRequest(
	TransferKind kind, const QStringList& rawSourcePaths, DestinationIntent intent, const QString& rawDestinationPath);
[[nodiscard]] std::expected<PermanentDeleteRequest, RequestValidationError> makePermanentDeleteRequest(const QStringList& rawSourcePaths);

[[nodiscard]] std::vector<RootTransferIntent> rootTransferIntents(const TransferRequest& request);

// --- Typed issues and decisions ---

// The six genuinely different user policy questions. Deliberately not a diagnostic-stage taxonomy:
// FailedAction supplies the wording detail, never the policy.
enum class IssueKind
{
	FileReplacement,
	RootDirectoryMerge,
	TypeMismatch,
	ActionFailed,
	ReadOnlySourceRemoval,
	UnsupportedEntry
};

enum class DecisionAction
{
	Skip,
	Replace,
	Merge,
	MakeWritable,
	Rename,
	Retry,
	Cancel
};

enum class DecisionScope
{
	ThisItem,
	RemainingMatchingIssues
};

// In UI presentation order; the exact set comes from the normative table row for the issue kind.
using AllowedActions = std::vector<DecisionAction>;

struct OperationIssue
{
	IssueKind kind;
	EntrySnapshot source;
	std::optional<EntrySnapshot> destination;
	std::optional<FailureDetails> failure; // Only for ActionFailed (and raced ReadOnlySourceRemoval detail)
};

// Neutral non-interactive record for the warning and terminal-failure channels. No issue kind, no legal
// actions, no remembered policy - a decision is never awaited on one of these.
struct OperationDiagnostic
{
	FailureDetails failure;
	EntrySnapshot source;
	std::optional<EntrySnapshot> destination;
};

struct DecisionRequest
{
	OperationIssue issue;
	AllowedActions allowedActions;
	bool remainingMatchingScopeAllowed;
};

struct Decision
{
	DecisionAction action;
	DecisionScope scope = DecisionScope::ThisItem;
	std::optional<QString> newName; // Required by Rename, meaningless otherwise
};

// The normative six-row policy table. These two functions are its only implementation; there is no
// second capability list, and the UI derives its buttons from the delivered AllowedActions.
[[nodiscard]] AllowedActions allowedActionsFor(IssueKind kind);
// Only a rememberable action may be offered or stored with RemainingMatchingIssues scope.
// Rename, Retry, and Cancel are never rememberable.
[[nodiscard]] bool isActionRememberable(IssueKind kind, DecisionAction action);

// --- Recursive execution result ---

enum class NodeOutcome
{
	Completed,        // Required work for the subtree finished (and, where applicable, cleanup succeeded)
	AlreadySatisfied, // The desired end state already held (same-object transfer, delete target absent); no mutation was needed
	Skipped,          // Deliberately left untouched
	Partial,          // Some work completed, but skipped/partial owned content prevents full completion; a non-failure result
	Failed,           // Required work did not complete; detail travels through the failure-diagnostic channel
	Cancelled         // Cancellation stops further traversal
};

// The one exact aggregation precedence: Cancelled wins, then Failed, then skipped/partial descendants
// make the containing subtree Partial; Completed and AlreadySatisfied children change nothing.
[[nodiscard]] NodeOutcome aggregateChildOutcome(NodeOutcome aggregate, NodeOutcome child) noexcept;

// --- Progress and completion ---

// Fixed per job at launch by the request kind: bytes for transfers, items for permanent delete.
// There is no unit switching mid-job.
enum class PrimaryProgressUnit
{
	Bytes,
	Items
};

enum class OperationPhase
{
	Scanning,
	Working
};

// Everything the dialog renders; the dialog formats, it does not calculate. The primary progress unit is
// not snapshot state - it is fixed per job at launch by the request kind (bytes for transfers, items for
// permanent delete). Unknown totals stay absent rather than zero.
struct ProgressSnapshot
{
	OperationPhase phase;
	std::optional<CEntryPath> currentEntry;

	uint64_t bytesProcessed = 0;
	std::optional<uint64_t> bytesTotal;
	uint64_t currentEntryBytesProcessed = 0;
	std::optional<uint64_t> currentEntryBytesTotal;

	size_t itemsProcessed = 0;
	std::optional<size_t> itemsTotal;

	uint64_t primaryUnitsPerSecond = 0;
	std::optional<uint32_t> secondsRemaining;
};

enum class CompletionStatus
{
	Completed,
	Cancelled,
	Failed
};

// The complete executor result and completion event. Diagnostics are bounded representatives;
// warningCount keeps the aggregate, and failedItems is the aggregate terminal-failure count.
struct OperationSummary
{
	CompletionStatus status = CompletionStatus::Completed;
	size_t completedItems = 0;
	size_t skippedItems = 0;
	size_t failedItems = 0;
	size_t alreadySatisfiedItems = 0;
	uint64_t transferredBytes = 0;
	size_t warningCount = 0;
	std::vector<OperationDiagnostic> representativeWarnings;
	std::vector<OperationDiagnostic> representativeFailures;
};
