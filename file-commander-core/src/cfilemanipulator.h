#pragma once

#include "fileoperationresultcode.h"
#include "cfilesystemobject.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <stdint.h>

class QFile;

class CFileManipulator
{
public:
	explicit CFileManipulator(const CFileSystemObject& object);

// Operations
	FileOperationResultCode copyAtomically(const QString& destFolder, const QString& newName = QString(), bool copyPermissions = true);
	FileOperationResultCode moveAtomically(const QString& destFolder, const QString& newName = QString());

	static FileOperationResultCode copyAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName = QString());
	static FileOperationResultCode moveAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName = QString());

	bool makeWritable(bool writable = true);
	FileOperationResultCode remove();

	static bool makeWritable(const CFileSystemObject& object, bool writable = true);
	static FileOperationResultCode remove(const CFileSystemObject& object);

// Non-blocking file copy API
	// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
	FileOperationResultCode copyChunk(size_t chunkSize, const QString& destFolder, const QString& newName = QString(), bool transferPermissions = true);
	FileOperationResultCode moveChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString());
	bool copyOperationInProgress() const;
	uint64_t bytesCopied() const;
	FileOperationResultCode cancelCopy();

// State
	QString lastErrorMessage() const;

private:
	static QString /* error text; empty on success */ copyPermissions(const QFile& sourceFile, QFile& destinationFile);
	static QString /* error text; empty on success */ copyPermissions(const QFile& sourceFile, const QString& destinationFilePath);

private:
	CFileSystemObject _object;

	// For copying / moving
	std::shared_ptr<QFile> _thisFile;
	std::shared_ptr<QFile> _destFile;
	uint64_t               _pos = 0;
	mutable QString        _lastErrorMessage;
};
