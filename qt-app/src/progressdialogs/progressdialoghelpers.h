#pragma once

#include "fileoperations/fileoperationtypes.h"

#include <stdint.h>

class QString;

QString secondsToTimeIntervalString(uint32_t secondsTotal);

// Shared user-facing formatting for file-operation types, used by both the decision prompt and the
// completion summary so the wording has a single source.
[[nodiscard]] QString fileOperationEntryKindNoun(OperationEntryKind kind);
[[nodiscard]] QString fileOperationFailedActionText(FailedAction action);
[[nodiscard]] QString fileSystemErrorText(const CFileSystemError& error);
// One line for a completion diagnostic: the affected entry, the attempted action, and the reason.
[[nodiscard]] QString fileOperationDiagnosticText(const OperationDiagnostic& diagnostic);
