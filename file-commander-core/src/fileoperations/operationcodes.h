#pragma once

enum Operation {operationCopy, operationMove, operationDelete};

// TODO: "*All" items are unnecessary, get rid of them
enum UserResponse {urSkipThis, urSkipAll, urProceedWithThis, urProceedWithAll, urRename, urAbort, urRetry, urNone};

enum HaltReason {hrFileExists, hrSourceFileIsReadOnly, hrDestFileIsReadOnly, hrFailedToMakeItemWritable, hrFileDoesntExit, hrCreatingFolderFailed, hrFailedToDelete, hrUnknownError};
