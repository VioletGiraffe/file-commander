#pragma once

#include "fileoperationresultcode.h"
#include "cfilesystemobject.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>

class QFile;

class CFileManipulator
{
public:
	explicit CFileManipulator(const CFileSystemObject& object);

// Operations
	FileOperationResultCode copyAtomically(const QString& destFolder, const QString& newName = QString());
	FileOperationResultCode moveAtomically(const QString& destFolder, const QString& newName = QString());

// Non-blocking file copy API
	// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
	FileOperationResultCode copyChunk(size_t chunkSize, const QString& destFolder, const QString& newName = QString());
	FileOperationResultCode moveChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString());
	bool copyOperationInProgress() const;
	uint64_t bytesCopied() const;
	FileOperationResultCode cancelCopy();

	bool                    makeWritable(bool writeable = true);
	FileOperationResultCode remove();

	QString lastErrorMessage() const;

private:
	CFileSystemObject _object;

	// For copying / moving
	std::shared_ptr<QFile> _thisFile;
	std::shared_ptr<QFile> _destFile;
	uint64_t               _pos = 0;
	mutable QString        _lastErrorMessage;
};
