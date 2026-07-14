#include "cfilemanipulator.h"
#include "filesystemhelperfunctions.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include "system/win_utils.hpp"
#include "windows/windowsutils.h"

#include <Windows.h>
#else
#include <errno.h>
#include <stdio.h> // ::rename
#include <sys/stat.h>
#include <unistd.h>
#endif

inline static QString getLastNativeFileError()
{
#ifdef _WIN32
	return QString::fromStdString(ErrorStringFromLastError());
#else
	return QString::fromLocal8Bit(strerror(errno));
#endif
}

#ifdef _WIN32
static bool setFileAttribute(const QString& path, const DWORD attribute, const bool enabled)
{
	WCHAR pathUnc[32768];
	toUncWcharArray(path, pathUnc);
	const DWORD attributes = ::GetFileAttributesW(pathUnc);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return false;

	const DWORD newAttributes = enabled ? attributes | attribute : attributes & ~attribute;
	return newAttributes == attributes || ::SetFileAttributesW(pathUnc, newAttributes) != 0;
}
#endif

using ThinIoErrorCode = decltype(thin_io::file::error_code());

static QString thinIoErrorMessage(const ThinIoErrorCode errorCode)
{
	return QString::fromStdString(thin_io::file::text_for_error(errorCode));
}

static bool isFileAlreadyExistsError(const ThinIoErrorCode errorCode)
{
#ifdef _WIN32
	return errorCode == ERROR_FILE_EXISTS || errorCode == ERROR_ALREADY_EXISTS;
#else
	return errorCode == EEXIST;
#endif
}

static bool isStorageExhaustedError(const ThinIoErrorCode errorCode)
{
#ifdef _WIN32
	return errorCode == ERROR_DISK_FULL || errorCode == ERROR_HANDLE_DISK_FULL || errorCode == ERROR_DISK_QUOTA_EXCEEDED;
#else
	return errorCode == ENOSPC || errorCode == EDQUOT;
#endif
}

static bool isUnsupportedPreallocationError(const ThinIoErrorCode errorCode)
{
#ifdef _WIN32
	return errorCode == ERROR_INVALID_FUNCTION || errorCode == ERROR_NOT_SUPPORTED || errorCode == ERROR_CALL_NOT_IMPLEMENTED || errorCode == ERROR_INVALID_PARAMETER;
#else
	// All arguments have already been validated, so EINVAL here means the filesystem cannot service the request.
	return errorCode == EOPNOTSUPP || errorCode == ENOSYS || errorCode == EINVAL;
#endif
}

static FileOperationResultCode classifyThinIoFailure(const ThinIoErrorCode errorCode, QString& errorMessage)
{
	errorMessage = thinIoErrorMessage(errorCode);
	return isStorageExhaustedError(errorCode) ? FileOperationResultCode::NotEnoughSpaceAvailable : FileOperationResultCode::Fail;
}

static FileOperationResultCode openTemporarySibling(thin_io::file& file, const QString& destinationFolder, QString& temporaryPath, QString& errorMessage)
{
	static constexpr int maxAttempts = 10;
	for (int attempt = 0; attempt < maxAttempts; ++attempt)
	{
		const QString path = destinationFolder % QStringLiteral(".file-commander-copy-") % QUuid::createUuid().toString(QUuid::WithoutBraces) % QStringLiteral(".tmp");
		const QByteArray encodedPath = path.toUtf8();
		if (file.open(encodedPath.constData(), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew))
		{
			temporaryPath = path;
			errorMessage.clear();
#ifdef _WIN32
			(void)setFileAttribute(path, FILE_ATTRIBUTE_HIDDEN, true);
#endif
			return FileOperationResultCode::Ok;
		}

		const auto errorCode = thin_io::file::error_code();
		if (!isFileAlreadyExistsError(errorCode))
			return classifyThinIoFailure(errorCode, errorMessage);
		errorMessage = thinIoErrorMessage(errorCode);
	}

	return FileOperationResultCode::Fail;
}

static FileOperationResultCode prepareStagingFile(thin_io::file& file, const uint64_t size, QString& errorMessage)
{
	if (!file.resize(size)) [[unlikely]]
	{
		const auto errorCode = thin_io::file::error_code();
		return classifyThinIoFailure(errorCode, errorMessage);
	}

	if (file.preallocate(size))
		return FileOperationResultCode::Ok;

	const auto errorCode = thin_io::file::error_code();
	if (isUnsupportedPreallocationError(errorCode))
		return FileOperationResultCode::Ok;

	return classifyThinIoFailure(errorCode, errorMessage);
}

static bool replaceFileEntry(const QString& sourcePath, const QString& destinationPath)
{
#ifdef _WIN32
	WCHAR sourcePathUnc[32768];
	WCHAR destinationPathUnc[32768];
	toUncWcharArray(sourcePath, sourcePathUnc);
	toUncWcharArray(destinationPath, destinationPathUnc);
	return ::MoveFileExW(sourcePathUnc, destinationPathUnc, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return ::rename(QFile::encodeName(sourcePath).constData(), QFile::encodeName(destinationPath).constData()) == 0;
#endif
}

// Types QFile::setFileTime() can write: access and modification on every platform, birth time only on Windows.
// (Qt rejects birth/metadata-change writes on Unix, and any invalid QDateTime, with EINVAL.) The copy reads only
// these from the source so it never reads a timestamp it cannot write back.
static constexpr QFileDevice::FileTime settableFileTimeTypes[] {
	QFileDevice::FileAccessTime,
#ifdef _WIN32
	QFileDevice::FileBirthTime,
#endif
	QFileDevice::FileModificationTime,
};

static QString fileTimeName(const QFileDevice::FileTime fileTimeType)
{
	switch (fileTimeType)
	{
	case QFileDevice::FileAccessTime:
		return QStringLiteral("access time");
	case QFileDevice::FileBirthTime:
		return QStringLiteral("creation time");
	case QFileDevice::FileMetadataChangeTime:
		return QStringLiteral("metadata change time");
	case QFileDevice::FileModificationTime:
		return QStringLiteral("modification time");
	default:
		assert_unconditional_r("Unknown QFileDevice::FileTime");
		return QStringLiteral("timestamp");
	}
}

// Operations
CFileManipulator::CFileManipulator(const CFileSystemObject& object) :
	_srcObject{ object }
{
}

FileOperationResultCode CFileManipulator::copyAtomically(const QString& destFolder, const QString& newName, TransferPermissions transferPermissions)
{
	assert_r(_srcObject.isFile());
	assert_r(QFileInfo{destFolder}.isDir());

	QFile file(_srcObject.fullAbsolutePath());
	const QString newFilePath = destFolder + (newName.isEmpty() ? _srcObject.fullName() : newName);
	bool succ = file.copy(newFilePath);
	if (succ)
	{
		if (transferPermissions)
		{
			_lastErrorMessage = copyPermissions(file, newFilePath);
			succ = _lastErrorMessage.isEmpty();
		}
	}
	else
		_lastErrorMessage = file.errorString();

	return succ ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
}

FileOperationResultCode CFileManipulator::moveAtomically(const QString& destFolder, const QString& newName, OverwriteExistingFile overwriteExistingFile)
{
	if (!_srcObject.exists())
		return FileOperationResultCode::ObjectDoesntExist;
	else if (_srcObject.isCdUp())
		return FileOperationResultCode::Fail;

	assert_debug_only(QFileInfo{ destFolder }.isDir());

	QString srcPath = _srcObject.fullAbsolutePath();
	// A link is moved as the link entry itself: the dir path's trailing slash must go
	// because "link/" resolves through the link, addressing the target instead of the link
	if (_srcObject.isLink() && srcPath.endsWith('/'))
		srcPath.chop(1);

	const QString fullNewName = destFolder % '/' % (newName.isEmpty() ? _srcObject.fullName() : newName);
	CFileSystemObject destInfo(fullNewName);
	const bool newNameDiffersOnlyInLetterCase = destInfo.fullAbsolutePath().compare(_srcObject.fullAbsolutePath(), Qt::CaseInsensitive) == 0;

	// If the file system is case-insensitive, and the source and destination only differ by case, renaming is allowed even though formally the destination already exists (fix for #102)
	if ((caseSensitiveFilesystem() || !newNameDiffersOnlyInLetterCase) && destInfo.exists())
	{
		if (_srcObject.isDir())
		{
			_lastErrorMessage = QStringLiteral("Replacing an existing item with a folder is not supported.");
			return FileOperationResultCode::TargetAlreadyExists;
		}

		// Replacing an existing file is only allowed when explicitly requested (https://github.com/VioletGiraffe/file-commander/issues/123),
		// and is performed atomically by the rename below
		if (destInfo.isFile() && !(overwriteExistingFile == true && _srcObject.isFile()))
			return FileOperationResultCode::TargetAlreadyExists;
	}

	// Windows: QFile::rename and QDir::rename fail to handle names that only differ by letter case (https://bugreports.qt.io/browse/QTBUG-3570)
	// Also, QFile::rename will attempt to painfully copy the file if it's locked.
#ifdef _WIN32
	// MOVEFILE_REPLACE_EXISTING is only valid for files, not directories
	const DWORD moveFlags = (overwriteExistingFile == true && _srcObject.isFile()) ? MOVEFILE_REPLACE_EXISTING : 0;
	if (MoveFileExW(reinterpret_cast<const WCHAR*>(srcPath.utf16()), reinterpret_cast<const WCHAR*>(destInfo.fullAbsolutePath().utf16()), moveFlags) != 0)
		return FileOperationResultCode::Ok;

	_lastErrorMessage = QString::fromStdString(ErrorStringFromLastError());
	return FileOperationResultCode::Fail;
#else
	if (_srcObject.isFile())
	{
		if (overwriteExistingFile == true)
		{
			// QFile::rename refuses to replace an existing file; ::rename() does, atomically
			if (::rename(QFile::encodeName(srcPath).constData(), QFile::encodeName(fullNewName).constData()) == 0)
				return FileOperationResultCode::Ok;

			_lastErrorMessage = strerror(errno);
			return FileOperationResultCode::Fail;
		}

		QFile file(srcPath);
		if (!file.rename(fullNewName))
		{
			_lastErrorMessage = file.errorString();
			return FileOperationResultCode::Fail;
		}

		//_srcObject.refreshInfo(); // TODO: what is this for?
		return FileOperationResultCode::Ok;
	}
	else if (_srcObject.isDir())
	{
		if (::rename(QFile::encodeName(srcPath).constData(), QFile::encodeName(fullNewName).constData()) == 0)
			return FileOperationResultCode::Ok;

		_lastErrorMessage = getLastNativeFileError();
		return FileOperationResultCode::Fail;
	}
	else
		return FileOperationResultCode::Fail;
#endif
}

FileOperationResultCode CFileManipulator::copyAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName /*= QString()*/)
{
	return CFileManipulator(object).copyAtomically(destFolder, newName);
}

FileOperationResultCode CFileManipulator::moveAtomically(const CFileSystemObject& object, const QString& destFolder, const QString& newName /*= QString()*/)
{
	return CFileManipulator(object).moveAtomically(destFolder, newName);
}

// Non-blocking file copy API

// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
FileOperationResultCode CFileManipulator::copyChunk(const uint64_t chunkSize, const QString& destFolder, const QString& newName /*= QString()*/, const bool transferPermissions, const bool transferDates)
{
	assert_debug_only(_srcObject.isFile());
	assert_debug_only(QFileInfo(destFolder).isDir());

	if (!copyOperationInProgress())
	{
		_pos = 0;
		_lastErrorMessage.clear();
		_destinationFilePath = destFolder + (newName.isEmpty() ? _srcObject.fullName() : newName);
		assert_debug_only(_temporaryDestinationFilePath.isEmpty());

		const auto sourceObjectId = resolvedObjectId(_srcObject.fullAbsolutePath());
		if (_destinationFilePath == _srcObject.fullAbsolutePath() || (sourceObjectId && sourceObjectId == resolvedObjectId(_destinationFilePath))) [[unlikely]]
		{
			_lastErrorMessage = QStringLiteral("Source and destination refer to the same file.");
			return FileOperationResultCode::Fail;
		}

		// Creating files
		_thisFile.setFileName(_srcObject.fullAbsolutePath());

		for (const auto fileTimeType: settableFileTimeTypes)
			_sourceFileTime[fileTimeType] = _thisFile.fileTime(fileTimeType);

		// Initializing - opening files
		if (!_thisFile.open(QFile::ReadOnly | QFile::Unbuffered)) [[unlikely]]
		{
			_lastErrorMessage = _thisFile.errorString();
			return FileOperationResultCode::Fail;
		}

		const auto openResult = openTemporarySibling(_stagingFile, destFolder, _temporaryDestinationFilePath, _lastErrorMessage);
		if (openResult != FileOperationResultCode::Ok) [[unlikely]]
		{
			_thisFile.close();
			return openResult;
		}

		// Set the final logical size, then reserve physical space when supported to detect storage exhaustion early and reduce fragmentation.
		const auto preparationResult = prepareStagingFile(_stagingFile, static_cast<uint64_t>(_srcObject.size()), _lastErrorMessage);
		if (preparationResult != FileOperationResultCode::Ok) [[unlikely]]
		{
			_stagingFile.close();
			_thisFile.close();
			if (QFile::remove(_temporaryDestinationFilePath))
				_temporaryDestinationFilePath.clear();
			return preparationResult;
		}
	}

	assert_debug_only(_stagingFile.is_open() == _thisFile.isOpen());

	const uint64_t srcSize = (uint64_t)_srcObject.size();
	const uint64_t actualChunkSize = std::min(chunkSize, srcSize - _pos);

	uint64_t bytesWritten = 0;
	if (actualChunkSize != 0)
	{
		auto* src = _thisFile.map((qint64)_pos, (qint64)actualChunkSize);
		if (src == nullptr) [[unlikely]]
		{
			_lastErrorMessage = _thisFile.errorString();
			return FileOperationResultCode::Fail;
		}

		const auto written = _stagingFile.write(src, actualChunkSize);
		if (!written) [[unlikely]]
		{
			const auto errorCode = thin_io::file::error_code();
			return classifyThinIoFailure(errorCode, _lastErrorMessage);
		}
		else if (*written == 0) [[unlikely]] // A zero-byte write for a non-empty chunk would stall the caller's copy loop forever
		{
			_lastErrorMessage = QStringLiteral("Failed to write to the destination file (zero bytes written).");
			return FileOperationResultCode::Fail;
		}

		bytesWritten = *written;
		_pos += bytesWritten;

		[[maybe_unused]] const bool unmapResult = _thisFile.unmap(src);
		assert_debug_only(unmapResult);
	}

	if (_pos == srcSize) // All copied, close the files and transfer attributes (if requested)
	{
		if (!_stagingFile.fdatasync()) [[unlikely]]
		{
			const auto errorCode = thin_io::file::error_code();
			return classifyThinIoFailure(errorCode, _lastErrorMessage);
		}

		if (!_stagingFile.close()) [[unlikely]]
		{
			const auto errorCode = thin_io::file::error_code();
			return classifyThinIoFailure(errorCode, _lastErrorMessage);
		}
		_thisFile.close();

		if (transferPermissions || transferDates)
		{
			QFile metadataFile(_temporaryDestinationFilePath);
			if (!metadataFile.open(QFile::ReadWrite | QFile::Unbuffered)) [[unlikely]]
			{
				_lastErrorMessage = metadataFile.errorString();
				return FileOperationResultCode::Fail;
			}

			if (transferPermissions)
			{
				_lastErrorMessage = copyPermissions(_thisFile, metadataFile);
				if (!_lastErrorMessage.isEmpty()) [[unlikely]]
					return FileOperationResultCode::Fail;
			}

			if (transferDates)
			{
				for (const auto fileTimeType : settableFileTimeTypes)
				{
					if (!metadataFile.setFileTime(_sourceFileTime[fileTimeType], fileTimeType)) [[unlikely]]
					{
						_lastErrorMessage = QStringLiteral("Failed to set the destination file's %1: %2").arg(fileTimeName(fileTimeType), metadataFile.errorString());
						return FileOperationResultCode::Fail;
					}
				}
			}

			if (!metadataFile.flush()) [[unlikely]]
			{
				_lastErrorMessage = metadataFile.errorString();
				return FileOperationResultCode::Fail;
			}
		}

#ifdef _WIN32
		if (!setFileAttribute(_temporaryDestinationFilePath, FILE_ATTRIBUTE_HIDDEN, false)) [[unlikely]]
		{
			_lastErrorMessage = getLastNativeFileError();
			return FileOperationResultCode::Fail;
		}
#endif

		if (!replaceFileEntry(_temporaryDestinationFilePath, _destinationFilePath)) [[unlikely]]
		{
			_lastErrorMessage = getLastNativeFileError();
			return FileOperationResultCode::Fail;
		}
		_temporaryDestinationFilePath.clear();
	}

	return FileOperationResultCode::Ok;
}

FileOperationResultCode CFileManipulator::moveChunk(uint64_t /*chunkSize*/, const QString &destFolder, const QString& newName)
{
	return moveAtomically(destFolder, newName);
}

bool CFileManipulator::copyOperationInProgress() const
{
	const bool isOpen = _stagingFile.is_open();
	assert_debug_only(isOpen == _thisFile.isOpen());
	return isOpen;
}

uint64_t CFileManipulator::bytesCopied() const
{
	return _pos;
}

FileOperationResultCode CFileManipulator::cancelCopy()
{
	if (_thisFile.isOpen())
		_thisFile.close();

	QString cleanupError;
	if (_stagingFile.is_open())
	{
		if (!_stagingFile.close())
			cleanupError = QStringLiteral("Failed to close the temporary file: ") % QString::fromStdString(thin_io::file::text_for_last_error());
	}

	if (!_temporaryDestinationFilePath.isEmpty())
	{
#ifdef _WIN32
		(void)setFileAttribute(_temporaryDestinationFilePath, FILE_ATTRIBUTE_READONLY, false);
#endif
		QFile temporaryFile(_temporaryDestinationFilePath);
		const bool deleted = temporaryFile.remove();
		if (deleted)
			_temporaryDestinationFilePath.clear();
		else
		{
			if (!cleanupError.isEmpty())
				cleanupError += '\n';
			cleanupError += QStringLiteral("Failed to remove the temporary file: ") % temporaryFile.errorString();
		}
	}

	if (cleanupError.isEmpty())
		return FileOperationResultCode::Ok;

	if (!_lastErrorMessage.isEmpty())
		_lastErrorMessage += '\n';
	_lastErrorMessage += cleanupError;
	return FileOperationResultCode::Fail;
}

bool CFileManipulator::makeWritable(bool writable)
{
	assert_and_return_message_r(_srcObject.isFile(), "This method only works for files", false);

#ifdef _WIN32
	WCHAR UNCPath[32768];
	toUncWcharArray(_srcObject.fullAbsolutePath(), UNCPath);
	const DWORD attributes = ::GetFileAttributesW(UNCPath);
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		_lastErrorMessage = QString::fromStdString(ErrorStringFromLastError());
		return false;
	}

	if (::SetFileAttributesW(UNCPath, writable ? (attributes & (~(uint32_t)FILE_ATTRIBUTE_READONLY)) : (attributes | FILE_ATTRIBUTE_READONLY)) != TRUE)
	{
		_lastErrorMessage = QString::fromStdString(ErrorStringFromLastError());
		return false;
	}

	return true;
#else
	struct stat fileInfo;

	const QByteArray fileName = _srcObject.fullAbsolutePath().toUtf8();
	if (stat(fileName.constData(), &fileInfo) != 0)
	{
		_lastErrorMessage = strerror(errno);
		return false;
	}

	if (chmod(fileName.constData(), writable ? (fileInfo.st_mode | S_IWUSR) : (fileInfo.st_mode & (~S_IWUSR))) != 0)
	{
		_lastErrorMessage = strerror(errno);
		return false;
	}

	return true;
#endif
}

bool CFileManipulator::makeWritable(const CFileSystemObject& object, bool writable /*= true*/)
{
	return CFileManipulator(object).makeWritable(writable);
}

FileOperationResultCode CFileManipulator::remove()
{
	assert_and_return_message_r(_srcObject.exists(), "Object doesn't exist", FileOperationResultCode::ObjectDoesntExist);

	if (_srcObject.isLink())
	{
		// Deleting a link removes the link entry itself; the target's contents must never be touched.
		// The dir path's trailing slash must go: "link/" resolves through the link on POSIX, addressing the target instead of the link.
		QString linkPath = _srcObject.fullAbsolutePath();
		if (linkPath.endsWith('/'))
			linkPath.chop(1);

		// A directory link is a directory entry: rmdir (RemoveDirectory) deletes it without following, but fails
		// on file symlinks, for which QFile::remove() (unlink / DeleteFile) does the job.
		if (QDir{}.rmdir(linkPath))
			return FileOperationResultCode::Ok;

		QFile linkFile(linkPath);
		if (linkFile.remove())
			return FileOperationResultCode::Ok;

		_lastErrorMessage = linkFile.errorString();
		return FileOperationResultCode::Fail;
	}
	else if (_srcObject.isFile())
	{
		QFile file(_srcObject.fullAbsolutePath());
		if (file.remove())
			return FileOperationResultCode::Ok;
		else
		{
			_lastErrorMessage = file.errorString();
			return FileOperationResultCode::Fail;
		}
	}
	else if (_srcObject.isDir())
	{
		assert_r(_srcObject.isEmptyDir());

		QString directoryPath = _srcObject.fullAbsolutePath();
		if (directoryPath.endsWith('/') && !QDir{directoryPath}.isRoot())
			directoryPath.chop(1);

#ifdef _WIN32
		WCHAR directoryPathUnc[32768];
		toUncWcharArray(directoryPath, directoryPathUnc);
		if (::RemoveDirectoryW(directoryPathUnc) != 0)
			return FileOperationResultCode::Ok;
#else
		if (::rmdir(QFile::encodeName(directoryPath).constData()) == 0)
			return FileOperationResultCode::Ok;
#endif

		_lastErrorMessage = getLastNativeFileError();
		return FileOperationResultCode::Fail;
	}
	else
		return FileOperationResultCode::Fail;
}

FileOperationResultCode CFileManipulator::remove(const CFileSystemObject& object)
{
	return CFileManipulator(object).remove();
}

QString CFileManipulator::lastErrorMessage() const
{
	return _lastErrorMessage;
}

QString CFileManipulator::copyPermissions(const QFile &sourceFile, QFile &destinationFile)
{
	if (!destinationFile.setPermissions(sourceFile.permissions())) // TODO: benchmark against the static version QFile::setPermissions(filePath, permissions)
		return destinationFile.errorString();
	else
		return {};
}

QString CFileManipulator::copyPermissions(const QFile &sourceFile, const QString &destinationFilePath)
{
	QFile destinationFile {destinationFilePath};
	return copyPermissions(sourceFile, destinationFile);
}
