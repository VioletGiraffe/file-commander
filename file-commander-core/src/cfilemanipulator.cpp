#include "cfilemanipulator.h"
#include "filesystemhelperfunctions.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include "system/win_utils.hpp"
#include "windows/windowsutils.h"

#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

inline static QString getLastFileError()
{
	return QString::fromStdString(thin_io::file::text_for_last_error());
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
	const QString fullNewName = destFolder % '/' % (newName.isEmpty() ? _srcObject.fullName() : newName);
	CFileSystemObject destInfo(fullNewName);
	const bool newNameDiffersOnlyInLetterCase = destInfo.fullAbsolutePath().compare(_srcObject.fullAbsolutePath(), Qt::CaseInsensitive) == 0;

	// If the file system is case-insensitive, and the source and destination only differ by case, renaming is allowed even though formally the destination already exists (fix for #102)
	if ((caseSensitiveFilesystem() || !newNameDiffersOnlyInLetterCase) && destInfo.exists())
	{
		if (_srcObject.isDir())
			return FileOperationResultCode::TargetAlreadyExists;
		else if (overwriteExistingFile == true && _srcObject.isFile() && destInfo.isFile())
		{
			// Special case: it may be allowed to replace the existing file (https://github.com/VioletGiraffe/file-commander/issues/123)
			if (remove(destInfo) != FileOperationResultCode::Ok)
				return FileOperationResultCode::TargetAlreadyExists;

			// File removed - update the info
			destInfo = CFileSystemObject{ fullNewName };
		}
		else if (destInfo.isFile())
			return FileOperationResultCode::TargetAlreadyExists;
	}

	// Windows: QFile::rename and QDir::rename fail to handle names that only differ by letter case (https://bugreports.qt.io/browse/QTBUG-3570)
	// Also, QFile::rename will attempt to painfully copy the file if it's locked.
#ifdef _WIN32
	if (MoveFileW(reinterpret_cast<const WCHAR*>(_srcObject.fullAbsolutePath().utf16()), reinterpret_cast<const WCHAR*>(destInfo.fullAbsolutePath().utf16())) != 0)
		return FileOperationResultCode::Ok;

	_lastErrorMessage = QString::fromStdString(ErrorStringFromLastError());
	return FileOperationResultCode::Fail;
#else
	if (_srcObject.isFile())
	{
		QFile file(_srcObject.fullAbsolutePath());
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
		return QDir{}.rename(_srcObject.fullAbsolutePath(), fullNewName) ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
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

		_destinationFilePath = destFolder + (newName.isEmpty() ? _srcObject.fullName() : newName);

		if (!_destFile.open(_destinationFilePath.toUtf8().constData(), thin_io::file_definitions::Write)) [[unlikely]]
		{
			_lastErrorMessage = getLastFileError();

			_thisFile.close();
			return FileOperationResultCode::Fail;
		}

		//if (!_destFile->truncate(_srcObject.size()))
		//{
		//	_lastErrorMessage = getLastFileError();
		//	assert_r(_destFile->close());
		//	assert_r(QFile::remove(_destinationFilePath));

		//	_thisFile.reset();
		//	_destFile.reset();

		//	return FileOperationResultCode::NotEnoughSpaceAvailable;
		//}
	}

	assert_debug_only(_destFile.is_open() == _thisFile.isOpen());

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

		const auto written = _destFile.write(src, actualChunkSize);
		if (!written) [[unlikely]]
		{
			_lastErrorMessage = getLastFileError();
			return FileOperationResultCode::Fail;
		}

		bytesWritten = *written;
		_pos += bytesWritten;

		[[maybe_unused]] const bool unmapResult = _thisFile.unmap(src);
		assert_debug_only(unmapResult);
	}

	if (_pos == srcSize) // All copied, close the files and transfer attributes (if requested)
	{
		if (!_destFile.close()) [[unlikely]]
		{
			_lastErrorMessage = getLastFileError();
			return FileOperationResultCode::Fail;
		}

		// Copying complete
		if (transferPermissions || transferDates)
		{
			assert_debug_only(!_destinationFilePath.isEmpty());
			// TODO: this is ineffective; need to support permission and date transfer in thin_io
			QFile qDstFile(_destinationFilePath);
			if (!qDstFile.open(QFile::ReadOnly | QFile::WriteOnly)) [[unlikely]]
			{
				_lastErrorMessage = qDstFile.errorString();
				return FileOperationResultCode::Fail;
			}

			if (transferPermissions)
				_lastErrorMessage = copyPermissions(_thisFile, qDstFile);

			if (transferDates)
			{
				// Note: The file must be open to use setFileTime()
				for (const auto fileTimeType : supportedFileTimeTypes)
					assert_r(qDstFile.setFileTime(_sourceFileTime[fileTimeType], fileTimeType));
			}
		}

		_thisFile.close();

		if (transferPermissions && !_lastErrorMessage.isEmpty()) [[unlikely]]
			return FileOperationResultCode::Fail;
	}

	return FileOperationResultCode::Ok;
}

FileOperationResultCode CFileManipulator::moveChunk(uint64_t /*chunkSize*/, const QString &destFolder, const QString& newName)
{
	return moveAtomically(destFolder, newName);
}

bool CFileManipulator::copyOperationInProgress() const
{
	const bool isOpen = _destFile.is_open();
	assert_debug_only(isOpen == _thisFile.isOpen());
	return isOpen;
}

uint64_t CFileManipulator::bytesCopied() const
{
	return _pos;
}

FileOperationResultCode CFileManipulator::cancelCopy()
{
	if (!copyOperationInProgress())
		return FileOperationResultCode::Ok;

	_thisFile.close();
	const bool closed = _destFile.close();

	const bool deleted = thin_io::file::delete_file(_destinationFilePath.toUtf8().constData());
	return closed && deleted ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
}

bool CFileManipulator::makeWritable(bool writable)
{
	assert_and_return_message_r(_srcObject.isFile(), "This method only works for files", false);

#ifdef _WIN32
	const QString UNCPath = toUncPath(_srcObject.fullAbsolutePath());
	const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(UNCPath.utf16()));
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		_lastErrorMessage = QString::fromStdString(ErrorStringFromLastError());
		return false;
	}

	if (SetFileAttributesW(reinterpret_cast<LPCWSTR>(UNCPath.utf16()), writable ? (attributes & (~(uint32_t)FILE_ATTRIBUTE_READONLY)) : (attributes | FILE_ATTRIBUTE_READONLY)) != TRUE)
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

	if (_srcObject.isFile())
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
