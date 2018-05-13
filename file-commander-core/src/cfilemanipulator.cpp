#include "cfilemanipulator.h"
#include "filesystemhelperfunctions.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QFile>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include "windows/windowsutils.h"

#include <Windows.h>
#endif

// Operations
CFileManipulator::CFileManipulator(const CFileSystemObject& object) : _object(object)
{
}

FileOperationResultCode CFileManipulator::copyAtomically(const QString& destFolder, const QString& newName)
{
	assert_r(_object.isFile());
	assert_r(QFileInfo(destFolder).isDir());

	QFile file (_object.fullAbsolutePath());
	const bool succ = file.copy(destFolder + (newName.isEmpty() ? _object.fullName() : newName));
	if (!succ)
		_lastErrorMessage = file.errorString();
	return succ ? rcOk : rcFail;
}

FileOperationResultCode CFileManipulator::moveAtomically(const QString& location, const QString& newName)
{
	if (!_object.exists())
		return rcObjectDoesntExist;
	else if (_object.isCdUp())
		return rcFail;

	assert_r(QFileInfo(location).isDir());
	const QString fullNewName = location % '/' % (newName.isEmpty() ? _object.fullName() : newName);
	const CFileSystemObject destInfo(fullNewName);
	const bool newNameDiffersOnlyInLetterCase = destInfo.fullAbsolutePath().compare(_object.fullAbsolutePath(), Qt::CaseInsensitive) == 0;
	if (destInfo.exists() && (_object.isDir() || destInfo.isFile()))
		// If the file system is case-insensitive, and the source and destination only differ by case, renaming is allowed even though formally the destination already exists (fix for #102)
		if (caseSensitiveFilesystem() || !newNameDiffersOnlyInLetterCase)
			return rcTargetAlreadyExists;

	// Special case for Windows, where QFile::rename and QDir::rename fail to handle names that only differ by letter case (https://bugreports.qt.io/browse/QTBUG-3570)
#ifdef _WIN32
	if (newNameDiffersOnlyInLetterCase)
	{
		if (MoveFileW((const WCHAR*)_object.fullAbsolutePath().utf16(), (const WCHAR*)destInfo.fullAbsolutePath().utf16()) != 0)
			return rcOk;

		_lastErrorMessage = ErrorStringFromLastError();
		return rcFail;
	}
#endif

	if (_object.isFile())
	{
		QFile file(_object.fullAbsolutePath());
		const bool succ = file.rename(fullNewName);
		if (!succ)
		{
			_lastErrorMessage = file.errorString();
			return rcFail;
		}

		_object.refreshInfo();
		return rcOk;
	}
	else if (_object.isDir())
	{
		return QDir().rename(_object.fullAbsolutePath(), fullNewName) ? rcOk : rcFail;
	}
	else
		return rcFail;
}


// Non-blocking file copy API

// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
FileOperationResultCode CFileManipulator::copyChunk(size_t chunkSize, const QString& destFolder, const QString& newName /*= QString()*/)
{
	assert_r(bool(_thisFile) == bool(_destFile));
	assert_r(_object.isFile());
	assert_r(QFileInfo(destFolder).isDir());

	if (!copyOperationInProgress())
	{
		_pos = 0;

		// Creating files
		_thisFile = std::make_shared<QFile>(_object.fullAbsolutePath());
		_destFile = std::make_shared<QFile>(destFolder + (newName.isEmpty() ? _object.fullName() : newName));

		// Initializing - opening files
		if (!_thisFile->open(QFile::ReadOnly))
		{
			_lastErrorMessage = _thisFile->errorString();

			_thisFile.reset();
			_destFile.reset();

			return rcFail;
		}

		if (!_destFile->open(QFile::ReadWrite))
		{
			_lastErrorMessage = _destFile->errorString();

			_thisFile.reset();
			_destFile.reset();

			return rcFail;
		}

		_destFile->resize(_object.size());
	}

	assert_r(_destFile->isOpen() == _thisFile->isOpen());

	const auto actualChunkSize = std::min(chunkSize, (size_t)(_object.size() - _pos));

	if (actualChunkSize != 0)
	{
		const auto src = _thisFile->map(_pos, actualChunkSize);
		if (!src)
		{
			_lastErrorMessage = _thisFile->errorString();
			return rcFail;
		}

		const auto dest = _destFile->map(_pos, actualChunkSize);
		if (!dest)
		{
			_lastErrorMessage = _destFile->errorString();
			return rcFail;
		}

		memcpy(dest, src, actualChunkSize);
		_pos += actualChunkSize;

		_thisFile->unmap(src);
		_destFile->unmap(dest);
	}

	if (actualChunkSize < chunkSize || actualChunkSize == 0)
	{
		_thisFile.reset();
		_destFile.reset();
	}

	return rcOk;
}

FileOperationResultCode CFileManipulator::moveChunk(uint64_t /*chunkSize*/, const QString &destFolder, const QString& newName)
{
	return moveAtomically(destFolder, newName);
}

bool CFileManipulator::copyOperationInProgress() const
{
	if (!_destFile && !_thisFile)
		return false;

	assert_r(_destFile->isOpen() == _thisFile->isOpen());
	return _destFile->isOpen() && _thisFile->isOpen();
}

uint64_t CFileManipulator::bytesCopied() const
{
	return _pos;
}

FileOperationResultCode CFileManipulator::cancelCopy()
{
	if (copyOperationInProgress())
	{
		_thisFile->close();
		_destFile->close();

		const bool succ = _destFile->remove();
		_thisFile.reset();
		_destFile.reset();
		return succ ? rcOk : rcFail;
	}
	else
		return rcOk;
}

bool CFileManipulator::makeWritable(bool writeable)
{
	assert_and_return_message_r(_object.isFile(), "This method only works for files", false);

#ifdef _WIN32
	const QString UNCPath = toUncPath(_object.fullAbsolutePath());
	const DWORD attributes = GetFileAttributesW((LPCWSTR)UNCPath.utf16());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		_lastErrorMessage = ErrorStringFromLastError();
		return false;
	}

	if (SetFileAttributesW((LPCWSTR) UNCPath.utf16(), writeable ? (attributes & (~FILE_ATTRIBUTE_READONLY)) : (attributes | FILE_ATTRIBUTE_READONLY)) != TRUE)
	{
		_lastErrorMessage = ErrorStringFromLastError();
		return false;
	}

	return true;
#else
	struct stat fileInfo;

	const QByteArray fileName = fullAbsolutePath().toUtf8();
	if (stat(fileName.constData(), &fileInfo) != 0)
	{
		_lastErrorMessage = strerror(errno);
		return false;
	}

	if (chmod(fileName.constData(), writeable ? (fileInfo.st_mode | S_IWUSR) : (fileInfo.st_mode & (~S_IWUSR))) != 0)
	{
		_lastErrorMessage = strerror(errno);
		return false;
	}

	return true;
#endif
}

FileOperationResultCode CFileManipulator::remove()
{
	qInfo() << "Removing" << _object.fullAbsolutePath();

	assert_and_return_message_r(_object.exists(), "Object doesn't exist", rcObjectDoesntExist);

	if (_object.isFile())
	{
		QFile file(_object.fullAbsolutePath());
		if (file.remove())
			return rcOk;
		else
		{
			_lastErrorMessage = file.errorString();
			return rcFail;
		}
	}
	else if (_object.isDir())
	{
		QDir dir (_object.fullAbsolutePath());
		assert_r(dir.entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty());
		errno = 0;
		if (!dir.rmdir("."))
		{
#if defined __linux || defined __APPLE__
//			dir.cdUp();
//			bool succ = dir.remove(_fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1));
//			qInfo() << "Removing " << _fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1) << "from" << dir.absolutePath();
			return ::rmdir(_properties.fullPath.toLocal8Bit().constData()) == -1 ? rcFail : rcOk;
//			return rcFail;
#else
			return rcFail;
#endif
		}
		return rcOk;
	}
	else
		return rcFail;
}

QString CFileManipulator::lastErrorMessage() const
{
	return _lastErrorMessage;
}

