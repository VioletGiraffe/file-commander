#pragma once

enum Operation {operationCopy, operationMove, operationDelete};

enum UserResponse {urSkipThis, urSkipAll, urProceedWithThis, urProceedWithAll, urRename, urAbort, urNone};

enum HaltReason {hrFileExists, hrSourceFileIsReadOnly, hrDestFileIsReadOnly, hrFileDoesntExit, hrUnknownError};
