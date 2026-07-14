#include "cfilemanipulator.h"
#include "filesystemhelperfunctions.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include "system/win_utils.hpp"
#include "windows/windowsutils.h"

#include <Windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <stdio.h> // ::rename
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <optional>

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

static bool openTemporarySibling(QFile& file, const QString& destinationFolder, QString& errorMessage)
{
	static constexpr int maxAttempts = 10;
	for (int attempt = 0; attempt < maxAttempts; ++attempt)
	{
		const QString path = destinationFolder % QStringLiteral(".file-commander-copy-") % QUuid::createUuid().toString(QUuid::WithoutBraces) % QStringLiteral(".tmp");
		file.setFileName(path);
		if (file.open(QFile::ReadWrite | QFile::NewOnly | QFile::Unbuffered))
		{
#ifdef _WIN32
			(void)setFileAttribute(path, FILE_ATTRIBUTE_HIDDEN, true);
#endif
			return true;
		}

		errorMessage = file.errorString();
	}

	return false;
}

static bool preallocateFile(QFile& file, const uint64_t size)
{
	if (size == 0)
		return true;

#ifdef _WIN32
	const HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(file.handle()));
	if (handle == INVALID_HANDLE_VALUE)
	{
		::SetLastError(ERROR_INVALID_HANDLE);
		return false;
	}

	FILE_END_OF_FILE_INFO eof;
	eof.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
	return ::SetFileInformationByHandle(handle, FileEndOfFileInfo, &eof, sizeof(eof)) != 0;
#elif defined __APPLE__
	fstore_t store;
	store.fst_flags = F_ALLOCATECONTIG;
	store.fst_posmode = F_PEOFPOSMODE;
	store.fst_offset = 0;
	store.fst_length = static_cast<off_t>(size);
	store.fst_bytesalloc = 0;
	if (::fcntl(file.handle(), F_PREALLOCATE, &store) == -1)
	{
		store.fst_flags = F_ALLOCATEALL;
		if (::fcntl(file.handle(), F_PREALLOCATE, &store) == -1)
			return false;
	}

	return ::ftruncate(file.handle(), static_cast<off_t>(size)) == 0;
#else
	const int error = ::posix_fallocate(file.handle(), 0, static_cast<off_t>(size));
	if (error != 0)
	{
		errno = error;
		return false;
	}

	return true;
#endif
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

static constexpr QFileDevice::FileTime supportedFileTimeTypes[] {
	QFileDevice::FileAccessTime,
#ifndef __linux__
	QFileDevice::FileBirthTime,
#endif
#if !defined __linux__ && !defined _WIN32
	QFileDevice::FileMetadataChangeTime,
#endif
	QFileDevice::FileModificationTime,
};

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
		return QDir{}.rename(srcPath, fullNewName) ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
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

		for (const auto fileTimeType: supportedFileTimeTypes)
			_sourceFileTime[fileTimeType] = _thisFile.fileTime(fileTimeType);

		// Initializing - opening files
		if (!_thisFile.open(QFile::ReadOnly | QFile::Unbuffered)) [[unlikely]]
		{
			_lastErrorMessage = _thisFile.errorString();
			return FileOperationResultCode::Fail;
		}

		if (!openTemporarySibling(_destinationFile, destFolder, _lastErrorMessage)) [[unlikely]]
		{
			_thisFile.close();
			return FileOperationResultCode::Fail;
		}
		_temporaryDestinationFilePath = _destinationFile.fileName();

		// Reserve the destination space up front: fail before copying if the disk is full, and reduce fragmentation
		if (!preallocateFile(_destinationFile, static_cast<uint64_t>(_srcObject.size()))) [[unlikely]]
		{
			_lastErrorMessage = getLastNativeFileError();
			_destinationFile.close();
			_thisFile.close();
			if (QFile::remove(_temporaryDestinationFilePath))
				_temporaryDestinationFilePath.clear();
			return FileOperationResultCode::NotEnoughSpaceAvailable;
		}
	}

	assert_debug_only(_destinationFile.isOpen() == _thisFile.isOpen());

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

		const qint64 written = _destinationFile.write(reinterpret_cast<const char*>(src), static_cast<qint64>(actualChunkSize));
		if (written < 0) [[unlikely]]
		{
			_lastErrorMessage = _destinationFile.errorString();
			return FileOperationResultCode::Fail;
		}
		else if (written == 0) [[unlikely]] // A zero-byte write for a non-empty chunk would stall the caller's copy loop forever
		{
			_lastErrorMessage = QStringLiteral("Failed to write to the destination file (zero bytes written).");
			return FileOperationResultCode::Fail;
		}

		bytesWritten = static_cast<uint64_t>(written);
		_pos += bytesWritten;

		[[maybe_unused]] const bool unmapResult = _thisFile.unmap(src);
		assert_debug_only(unmapResult);
	}

	if (_pos == srcSize) // All copied, close the files and transfer attributes (if requested)
	{
		// Copying complete
		if (transferPermissions)
		{
			_lastErrorMessage = copyPermissions(_thisFile, _destinationFile);
			if (!_lastErrorMessage.isEmpty()) [[unlikely]]
				return FileOperationResultCode::Fail;
		}

		if (transferDates)
		{
			// Note: The file must be open to use setFileTime()
			for (const auto fileTimeType : supportedFileTimeTypes)
				assert_r(_destinationFile.setFileTime(_sourceFileTime[fileTimeType], fileTimeType));
		}

		if (!_destinationFile.flush()) [[unlikely]]
		{
			_lastErrorMessage = _destinationFile.errorString();
			return FileOperationResultCode::Fail;
		}
		_destinationFile.close();
		_thisFile.close();

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
	const bool isOpen = _destinationFile.isOpen();
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
	bool closed = true;
	if (_destinationFile.isOpen())
	{
		closed = _destinationFile.flush();
		_destinationFile.close();
	}

	bool deleted = true;
	if (!_temporaryDestinationFilePath.isEmpty())
	{
#ifdef _WIN32
		(void)setFileAttribute(_temporaryDestinationFilePath, FILE_ATTRIBUTE_READONLY, false);
#endif
		deleted = QFile::remove(_temporaryDestinationFilePath);
		if (deleted)
			_temporaryDestinationFilePath.clear();
	}

	return closed && deleted ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
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
		errno = 0;
		if (!QDir{_srcObject.fullAbsolutePath()}.rmdir(QStringLiteral(".")))
		{
#if defined __linux || defined __APPLE__ || defined __FreeBSD__
			if (::rmdir(_srcObject.fullAbsolutePath().toLocal8Bit().constData()) == 0)
				return FileOperationResultCode::Ok;

			_lastErrorMessage = strerror(errno);
			return FileOperationResultCode::Fail;
#else
			return FileOperationResultCode::Fail;
#endif
		}
		return FileOperationResultCode::Ok;
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
