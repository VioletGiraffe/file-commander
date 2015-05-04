#include "cfilesystemobject.h"
#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"

#include <assert.h>
#include "fasthash.h"

#if defined __linux__ || defined __APPLE__
#include <unistd.h>
#include <errno.h>
#elif defined _WIN32
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib") // This lib would have to be added not just to the top level application, but every plugin as well, so using #pragma instead
#endif

CFileSystemObject::CFileSystemObject() : _type(UnknownType)
{
}

CFileSystemObject::CFileSystemObject(const QFileInfo& fileInfo) : _fileInfo(fileInfo), _type(UnknownType)
{
	refreshInfo();
}

CFileSystemObject::~CFileSystemObject()
{
}

void CFileSystemObject::refreshInfo()
{
	_properties.exists = _fileInfo.exists();
	_properties.fullPath = _fileInfo.absoluteFilePath();

	const QByteArray utf8Path = _properties.fullPath.toUtf8();
	_properties.hash = fasthash64(utf8Path.constData(), utf8Path.size(), 0);

	if (!_properties.exists)
		return; // Symlink pointing to a non-existing file - skipping

	_properties.creationDate = (time_t) _fileInfo.created().toTime_t();

	if (_fileInfo.isFile())
		_type = File;
	else if (_fileInfo.isDir())
		_type = Directory;
	else
	{
#ifdef _WIN32
		qDebug() << _properties.fullPath << " is neither a file nor a dir";
#endif
		return;
	}

	if (_type != Directory)
	{
		_properties.extension = _fileInfo.suffix();
		_properties.completeBaseName = _fileInfo.completeBaseName();
	}
	else
	{
		const QString suffix = _fileInfo.completeSuffix();
		_properties.completeBaseName = _fileInfo.baseName();
		if (!suffix.isEmpty())
			_properties.completeBaseName += "." + suffix;
	}

	_properties.fullName = _type == Directory ? _properties.completeBaseName : _fileInfo.fileName();
	_properties.parentFolder = _fileInfo.absolutePath();
	_properties.modificationDate = _fileInfo.lastModified().toTime_t();
	_properties.size = _type == File ? _fileInfo.size() : 0;
	_properties.type = _type;
}

bool CFileSystemObject::operator==(const CFileSystemObject& other) const
{
	return hash() == other.hash();
}


// Information about this object
bool CFileSystemObject::exists() const
{
	return _properties.exists;
}

const CFileSystemObjectProperties &CFileSystemObject::properties() const
{
	return _properties;
}

FileSystemObjectType CFileSystemObject::type() const
{
	return _type;
}

bool CFileSystemObject::isFile() const
{
	return _type == File;
}

bool CFileSystemObject::isDir() const
{
	return _type == Directory;
}

bool CFileSystemObject::isEmptyDir() const
{
	return isDir()? QDir(fullAbsolutePath()).entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty() : false;
}

bool CFileSystemObject::isCdUp() const
{
	return _fileInfo.fileName() == "..";
}

bool CFileSystemObject::isExecutable() const
{
	return _fileInfo.permission(QFile::ExeUser) || _fileInfo.permission(QFile::ExeOwner) || _fileInfo.permission(QFile::ExeGroup) || _fileInfo.permission(QFile::ExeOther);
}

bool CFileSystemObject::isReadable() const
{
	return _fileInfo.isReadable();
}

bool CFileSystemObject::isWriteable() const
{
	return _fileInfo.isWritable();
}

bool CFileSystemObject::isHidden() const
{
	return _fileInfo.isHidden();
}

// Returns true if this object is a child of parent, either direct or indirect
bool CFileSystemObject::isChildOf(const CFileSystemObject &parent) const
{
	return fullAbsolutePath().startsWith(parent.fullAbsolutePath(), Qt::CaseInsensitive);
}

QString CFileSystemObject::fullAbsolutePath() const
{
	return _properties.fullPath;
}

QString CFileSystemObject::parentDirPath() const
{
	return _properties.parentFolder;
}

const QIcon& CFileSystemObject::icon() const
{
	return CIconProvider::iconForFilesystemObject(*this);
}

uint64_t CFileSystemObject::size() const
{
	return _properties.size;
}

qulonglong CFileSystemObject::hash() const
{
	return _properties.hash;
}

const QFileInfo &CFileSystemObject::qFileInfo() const
{
	return _fileInfo;
}

std::vector<QString> CFileSystemObject::pathHierarchy() const
{
	QString path = fullAbsolutePath();
	std::vector<QString> result(1, path);
	while ((path = QFileInfo(path).path()).length() < result.back().length())
		result.push_back(path);

	return result;
}

bool CFileSystemObject::isMovableTo(const CFileSystemObject& dest) const
{
	const auto fileSystemId = rootFileSystemId(), otherFileSystemId = dest.rootFileSystemId();
	return fileSystemId == otherFileSystemId && fileSystemId != std::numeric_limits<uint64_t>::max() && otherFileSystemId != std::numeric_limits<uint64_t>::max();
}

// A hack to store the size of a directory after it's calculated
void CFileSystemObject::setDirSize(uint64_t size)
{
	_properties.size = size;
}

// File name without suffix, or folder name
QString CFileSystemObject::name() const
{
	return _properties.completeBaseName;
}

// Filename + suffix for files, same as name() for folders
QString CFileSystemObject::fullName() const
{
	return _properties.fullName;
}

QString CFileSystemObject::extension() const
{
	if (_properties.type == File && _properties.completeBaseName.isEmpty()) // File without a name, displaying extension in the name field and adding point to extension
		return QString('.') + _properties.extension;
	else
		return _properties.extension;
}

QString CFileSystemObject::sizeString() const
{
	return _type == File ? fileSizeToString(_properties.size) : QString();
}

QString CFileSystemObject::modificationDateString() const
{
	QDateTime modificationDate;
	modificationDate.setTime_t((uint)_properties.modificationDate);
	modificationDate = modificationDate.toLocalTime();
	return modificationDate.toString("dd.MM.yyyy hh:mm");
}


// Operations
FileOperationResultCode CFileSystemObject::copyAtomically(const QString& destFolder, const QString& newName)
{
	assert(isFile());
	assert(QFileInfo(destFolder).isDir());

	QFile file (_properties.fullPath);
	const bool succ = file.copy(destFolder + (newName.isEmpty() ? _fileInfo.fileName() : newName));
	if (!succ)
		_lastError = file.errorString();
	return succ ? rcOk : rcFail;
}

FileOperationResultCode CFileSystemObject::moveAtomically(const QString& location, const QString& newName)
{
	if (!exists())
		return rcObjectDoesntExist;

	assert(QFileInfo(location).isDir());
	const QString fullNewName = location + "/" + (newName.isEmpty() ? _fileInfo.fileName() : newName);
	const QFileInfo destInfo(fullNewName);
	if (destInfo.exists() && (isDir() || destInfo.isFile()))
		return rcTargetAlreadyExists;

	if (isFile())
	{
		QFile file(_properties.fullPath);
		const bool succ = file.rename(fullNewName);
		if (!succ)
		{
			_lastError = file.errorString();
			return rcFail;
		}

		refreshInfo();
		return rcOk;
	}
	else if (isDir())
	{
		return QDir().rename(fullAbsolutePath(), fullNewName) ? rcOk : rcFail;
	}
	else
		return rcFail;
}


// Non-blocking file copy API

// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
FileOperationResultCode CFileSystemObject::copyChunk(int64_t chunkSize, const QString& destFolder, const QString& newName)
{
	assert(bool(_thisFile) == bool(_destFile));
	assert(isFile());
	assert(QFileInfo(destFolder).isDir());

	if (!copyOperationInProgress())
	{
		// Creating files
		if (!_thisFile)
		{
			_thisFile = std::make_shared<QFile>();
			_destFile = std::make_shared<QFile>();
		}

		// Initializing - opening files
		_thisFile->setFileName(fullAbsolutePath());
		if (!_thisFile->open(QFile::ReadOnly))
		{
			_lastError = _thisFile->errorString();
			return rcFail;
		}

		_destFile->setFileName(destFolder + (newName.isEmpty() ? _fileInfo.fileName() : newName));
		if (!_destFile->open(QFile::WriteOnly))
		{
			_lastError = _destFile->errorString();
			return rcFail;
		}
	}

	assert(_destFile->isOpen() == _thisFile->isOpen());

	QByteArray data = _thisFile->read(chunkSize);
	if (data.isEmpty())
	{
		_destFile->close();
		_thisFile->close();
		return rcOk;
	}
	else
	{
		if (_destFile->write(data) == data.size())
			return rcOk;
		else
		{
			_lastError = _thisFile->error() != QFile::NoError ? _thisFile->errorString() : _destFile->errorString();
			return rcFail;
		}
	}
}

FileOperationResultCode CFileSystemObject::moveChunk(int64_t /*chunkSize*/, const QString &destFolder, const QString& newName)
{
	return moveAtomically(destFolder, newName);
}

bool CFileSystemObject::copyOperationInProgress() const
{
	if (!_destFile && !_thisFile)
		return false;

	assert(_destFile->isOpen() == _thisFile->isOpen());
	return _destFile->isOpen() && _thisFile->isOpen();
}

uint64_t CFileSystemObject::bytesCopied() const
{
	return (_thisFile && _thisFile->isOpen()) ? (uint64_t)_thisFile->pos() : 0;
}

FileOperationResultCode CFileSystemObject::cancelCopy()
{
	if(copyOperationInProgress())
	{
		_thisFile->close();
		_destFile->close();
		return _destFile->remove() ? rcOk : rcFail;
	}
	else
		return rcOk;
}

bool CFileSystemObject::makeWritable()
{
	QFile file (_fileInfo.absoluteFilePath());
	if (file.setPermissions(file.permissions() | QFile::WriteUser))
		return true;
	else
	{
		_lastError = file.errorString();
		return false;
	}
}

FileOperationResultCode CFileSystemObject::remove()
{
	qDebug() << "Removing" << _properties.fullPath;
	if (!_fileInfo.exists())
	{
		Q_ASSERT_X(false, "CFileSystemObject::remove()", "Object doesn't exist");
		return rcObjectDoesntExist;
	}
	else if (isFile())
	{
		QFile file(_properties.fullPath);
		if (file.remove())
			return rcOk;
		else
		{
			_lastError = file.errorString();
			return  rcFail;
		}
	}
	else if (isDir())
	{
		QDir dir (_properties.fullPath);
		assert(dir.isReadable());
		assert(dir.entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty());
		errno = 0;
		if (!dir.rmdir("."))
		{
#if defined __linux || defined __APPLE__
//			dir.cdUp();
//			bool succ = dir.remove(_fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1));
//			qDebug() << "Removing " << _fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1) << "from" << dir.absolutePath();
			return ::rmdir(__properties.fullPath.toLocal8Bit().constData()) == -1 ? rcFail : rcOk;
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

QString CFileSystemObject::lastErrorMessage() const
{
	return _lastError;
}

uint64_t CFileSystemObject::rootFileSystemId() const
{
	if (_rootFileSystemId == std::numeric_limits<uint64_t>::max())
	{
#ifdef _WIN32
		const auto driveNumber = PathGetDriveNumberW((WCHAR*)_properties.fullPath.utf16());
		if (driveNumber != -1)
			_rootFileSystemId = (uint64_t)driveNumber;
#else
		struct stat info;
		const int ret = stat(_properties.fullPath.toUtf8().constData(), &info);
		if (ret == 0)
			_rootFileSystemId = (uint64_t)info.st_dev;
		else
		{
			_lastError = strerror(errno);
			qDebug() << __FUNCTION__ << "Failed to query device ID for" << _properties.fullPath;
		}
#endif
	}

	return _rootFileSystemId;
}

