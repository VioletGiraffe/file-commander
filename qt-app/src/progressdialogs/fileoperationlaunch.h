#pragma once

#include "fileoperations/fileoperationtypes.h"

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>

// The UI-side launch boundary. It converts the panel's raw selection and the edited destination text into a
// trusted core request, applying the preserved destination-intent heuristic exactly once - no resolver,
// executor, or mutator ever re-derives intent. These are free functions, not a class: there is no launch
// state to own, and keeping them separate from CMainWindow is what lets them be tested without an application.

// The preserved heuristic: multiple sources, or a single directory-like source, map into the destination
// directory; a single file is an exact target unless the edited destination already exists as a directory.
// Inspects the filesystem for the single-source kind and the destination kind.
[[nodiscard]] DestinationIntent transferDestinationIntent(const QStringList& rawSourcePaths, const QString& destinationText);

// The confirmation field's prefill: the exact target path for a one-file copy, the directory itself otherwise.
[[nodiscard]] QString prefillTransferDestination(TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationDirectory);

// Builds the trusted request from the raw UI selection and the edited destination, choosing the intent once.
[[nodiscard]] std::expected<TransferRequest, RequestValidationError> makeUiTransferRequest(
	TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationText);

// Which backend performs a deletion. Selection is compile-time per platform and preserves current routing;
// only the InternalJob case flows through the custom PermanentDeleteRequest / job / dialog.
enum class DeletionBackend
{
	NativeTrash,          // Native shell trash (Windows, macOS)
	NativeShellPermanent, // Native shell permanent deletion (Windows)
	InternalJob           // The custom permanent-delete engine (everywhere the native shell is not used)
};

[[nodiscard]] DeletionBackend deletionBackendFor(bool toTrash);
