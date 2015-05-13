#pragma once

enum Operation {operationCopy, operationMove, operationDelete};

enum UserResponse {urSkipThis, urSkipAll, urProceedWithThis, urProceedWithAll, urRename, urAbort, urRetry, urNone};

enum HaltReason {hrFileExists, hrSourceFileIsReadOnly, hrDestFileIsReadOnly, hrFileDoesntExit, hrCreatingFolderFailed, hrFailedToDelete, hrUnknownError};
