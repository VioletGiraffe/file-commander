#pragma once

#include "fileoperationresultcode.h"
#include "cfilesystemobject.h"
#include "compiler/compiler_warnings_control.h"
#include "utility/named_type_wrapper.hpp"

#include "file.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QFile>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <memory>
#include <stdint.h>

using TransferPermissions = UniqueNamedBoolType;
using OverwriteExistingFile = UniqueNamedBoolType;

class CFileManipulator
{
public:
	explicit CFileManipulator(const CFileSystemObject& object);

// Operations

	[[nodiscard]] FileOperationResultCode copyAtomically(const QString& destFolder, const QString& newName = {}, TransferPermissions transferPermissions = TransferPermissions{ true });
	[[nodiscard]] FileOperationResultCode moveAtomically(const QString& destFolder, const QString& newName = {}, OverwriteExistingFile overwriteExistingFile = {});

	[[nodiscard]] static FileOperationResultCode copyAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName = QString());
	[[nodiscard]] static FileOperationResultCode moveAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName = QString());

	[[nodiscard]] bool makeWritable(bool writable = true);
	[[nodiscard]] FileOperationResultCode remove();

	[[nodiscard]] static bool makeWritable(const CFileSystemObject& object, bool writable = true);
	[[nodiscard]] static FileOperationResultCode remove(const CFileSystemObject& object);

// Non-blocking file copy API
	// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
	[[nodiscard]] FileOperationResultCode copyChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString(), bool transferPermissions = true, bool transferDates = true);
	[[nodiscard]] FileOperationResultCode moveChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString());
	[[nodiscard]] bool copyOperationInProgress() const;
	[[nodiscard]] uint64_t bytesCopied() const;
	[[nodiscard]] FileOperationResultCode cancelCopy();

// State
	[[nodiscard]] QString lastErrorMessage() const;

private:
	static QString /* error text; empty on success */ copyPermissions(const QFile& sourceFile, QFile& destinationFile);
	static QString /* error text; empty on success */ copyPermissions(const QFile& sourceFile, const QString& destinationFilePath);

private:
	const CFileSystemObject& _srcObject;
	QString _destinationFilePath;

	// For copying / moving
	std::array<QDateTime, 4> _sourceFileTime;
	QFile _thisFile;
	thin_io::file _destFile;
	uint64_t               _pos = 0;
	mutable QString        _lastErrorMessage;
};
