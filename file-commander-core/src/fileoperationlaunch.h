#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/fileoperationtypes.h"


DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>

// The launch boundary: turns a front end's selected paths and edited destination text into a trusted request.
// The destination intent is decided here, once: no resolver, executor or mutator derives it again.

// Multiple sources, a destination with an explicit trailing platform separator, or a single directory-like source
// map into the destination directory; otherwise a single file is an exact target unless the edited destination
// already exists as a directory.
// Inspects the filesystem for the single-source kind and the destination kind.
[[nodiscard]] DestinationIntent transferDestinationIntent(const QStringList& rawSourcePaths, const QString& destinationText);

// The confirmation field's prefill: the exact target path for a one-file copy, the directory itself otherwise.
[[nodiscard]] QString prefillTransferDestination(TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationDirectory);

// Builds the trusted request from the selected paths and the edited destination, choosing the intent once.
// The destination is typed path text, resolved against destinationFolder: see resolveUserPath().
[[nodiscard]] std::expected<TransferRequest, RequestValidationError> makeUiTransferRequest(
	TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationText, const QString& destinationFolder);

// Which backend performs a deletion
enum class DeletionBackend
{
	NativeShellTrash,     // The desktop shell's trash: Windows Explorer's, with its own progress, prompts and errors, or macOS's
	QtTrash,              // QFile::moveToTrash item by item: the freedesktop.org trash on Linux
	NativeShellPermanent, // Native shell permanent deletion (Windows)
	InternalJob           // The custom permanent-delete engine (everywhere the native shell is not used)
};

// Whether a front end hands deletions to the desktop shell
enum class DesktopShellUse
{
	Allowed,
	Never // A terminal UI, which may run over SSH
};

[[nodiscard]] DeletionBackend deletionBackendFor(bool toTrash, DesktopShellUse shellUse);
