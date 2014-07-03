#include "cfilesystemobject.h"
#include "iconprovider/ciconprovider.h"

#include <assert.h>

#if defined __linux__ || defined __APPLE__
#include <unistd.h>
#include <errno.h>
#endif

CFileSystemObject::CFileSystemObject() : _type(UnknownType)
{
}

CFileSystemObject::CFileSystemObject(const QFileInfo& fileInfo) : _fileInfo(fileInfo), _type(UnknownType)
{
	if (!fileInfo.exists())
		return; // Symlink pointing to a non-existing file - skipping

	if (fileInfo.isFile())
		_type = File;
	else if (fileInfo.isDir())
		_type = Directory;
	else
	{
#ifdef _WIN32
		qDebug() << fileInfo.absoluteFilePath() << " is neither a file nor a dir";
#endif
		return;
	}

	_properties.creationDate      = (time_t)_fileInfo.created().toTime_t();
	if (_type != Directory)
	{
		_properties.extension     = _fileInfo.suffix();
		_properties.name          = _fileInfo.completeBaseName();
	}
	else
		_properties.name          = _fileInfo.fileName();

	_properties.parentFolder      = parentDirPath();
	_properties.fullPath          = absoluteFilePath();
	_properties.modificationDate  = _fileInfo.lastModified().toTime_t();
	_properties.size              = _fileInfo.size();
	_properties.type              = _type;

	const QByteArray hash = QCryptographicHash::hash(_properties.fullPath.toUtf8(), QCryptographicHash::Md5);
	assert(hash.size() == 16);
	_properties.hash              = *(qulonglong*)(hash.data()) ^ *(qulonglong*)(hash.data()+8);

	_properties.permissions.read  = _fileInfo.isReadable();
	_properties.permissions.write = _fileInfo.isWritable();
	_properties.permissions.exec  = _fileInfo.isExecutable();
}

CFileSystemObject::~CFileSystemObject()
{
}


// Information about this object
bool CFileSystemObject::exists() const
{
	return _fileInfo.exists();
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

bool CFileSystemObject::isCdUp() const
{
	return _fileInfo.fileName() == "..";
}

bool CFileSystemObject::isExecutable() const
{
	return _fileInfo.permission(QFile::ExeUser) || _fileInfo.permission(QFile::ExeOwner) || _fileInfo.permission(QFile::ExeGroup) || _fileInfo.permission(QFile::ExeOther);
}

bool CFileSystemObject::isHidden() const
{
	return _fileInfo.isHidden();
}

// Returns true if this object is a child of parent, either direct or indirect
bool CFileSystemObject::isChildOf(const CFileSystemObject &parent) const
{
	return absoluteFilePath().startsWith(parent.absoluteFilePath(), Qt::CaseInsensitive);
}

QString CFileSystemObject::absoluteFilePath() const
{
	return toNativeSeparators(_fileInfo.absoluteFilePath());
}

QString CFileSystemObject::parentDirPath() const
{
	return toNativeSeparators(_fileInfo.absolutePath());
}

const QIcon& CFileSystemObject::icon() const
{
	return CIconProvider::iconForFilesystemObject(*this);
}

uint64_t CFileSystemObject::size() const
{
	return _properties.size;
}

// A hack to store the size of a directory after it's calculated
void CFileSystemObject::setDirSize(uint64_t size)
{
	_properties.size = size;
}


// Information as should be displayed in the UI
QString CFileSystemObject::name() const
{
	return _type == Directory ? QString("[%1]").arg(_properties.name) : _properties.name;
}

QString CFileSystemObject::extension() const
{
	if (_properties.type == File && _properties.name.isEmpty()) // File without a name, displaying extension in the name field and adding point to extension
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
FileOperationResultCode CFileSystemObject::rename(const QString &newName, bool relativeName)
{
	if (!exists())
	{
		assert(exists());
		return rcObjectDoesntExist;
	}
	else if (isFile())
	{
		QFile file(_fileInfo.absoluteFilePath());
		const QString newPath = relativeName ? QDir(parentDirPath()).absoluteFilePath(newName) : newName;
		if (file.rename(newPath))
			return rcOk;
		else
		{
			_lastError = file.errorString();
			return rcFail;
		}
	}
	else if (isDir())
	{
		QDir dir(_fileInfo.absoluteFilePath());
		if (dir.rename(".", newName))
			return rcOk;
		else
			return rcFail;
	}

	return rcFail;
}

FileOperationResultCode CFileSystemObject::copyAtomically(const QString& destFolder, const QString &newName)
{
	assert(isFile());
	assert(QFileInfo(destFolder).isDir());

	QFile file (_fileInfo.absoluteFilePath());
	const bool succ = file.copy(destFolder + (newName.isEmpty() ? _fileInfo.fileName() : newName));
	if (!succ)
		_lastError = file.errorString();
	return succ ? rcOk : rcFail;
}

FileOperationResultCode CFileSystemObject::moveAtomically(const QString& location, const QString &newName)
{
	assert(isFile());
	assert(QFileInfo(location).isDir());

	QFile file (_fileInfo.absoluteFilePath());
	const bool succ = file.rename(location + (newName.isEmpty() ? _fileInfo.fileName() : newName));

	if (!succ)
		_lastError = file.errorString();
	return succ ? rcOk : rcFail;

}


// Non-blocking file copy API

// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
FileOperationResultCode CFileSystemObject::copyChunk(int64_t chunkSize, const QString &destFolder, const QString &newName)
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
		_thisFile->setFileName(absoluteFilePath());
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
	assert(copyOperationInProgress());
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
	qDebug() << "Removing" << _fileInfo.absoluteFilePath();
	if (isFile())
	{
		QFile file (_fileInfo.absoluteFilePath());
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
		QDir dir (_fileInfo.absoluteFilePath());
		assert(dir.exists());
		assert(dir.isReadable());
		assert(dir.entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty());
		errno = 0;
		if (!dir.rmdir("."))
		{
#if defined __linux || defined __APPLE__
//			dir.cdUp();
//			bool succ = dir.remove(_fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1));
//			qDebug() << "Removing " << _fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1) << "from" << dir.absolutePath();
			return ::rmdir(_fileInfo.absoluteFilePath().toLocal8Bit().constData()) == -1 ? rcFail : rcOk;
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

QString CFileSystemObject::fileName() const
{
	return _fileInfo.fileName();
}

qulonglong CFileSystemObject::hash() const
{
	return _properties.hash;
}

const QFileInfo &CFileSystemObject::qFileInfo() const
{
	return _fileInfo;
}

std::vector<CFileSystemObject> recurseDirectoryItems(const QString &dirPath, bool includeFolders)
{
	std::vector<CFileSystemObject> objects;
	if (QFileInfo(dirPath).isDir())
	{
		QDir dir (dirPath);
		assert (dir.exists());
		QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs |  QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::System);
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if(it->isDir())
			{
				auto childrenItems = recurseDirectoryItems(it->absoluteFilePath(), includeFolders);
				objects.insert(objects.end(), childrenItems.begin(), childrenItems.end());
				if (includeFolders)
					objects.emplace_back(CFileSystemObject(*it));
			}
			else if (it->isFile())
				objects.push_back(CFileSystemObject(*it));
		}
	}
	else
		objects.push_back(CFileSystemObject(QFileInfo(dirPath)));

	return objects;
}

QString fileSizeToString(uint64_t size)
{
	const unsigned int KB = 1024;
	const unsigned int MB = 1024 * KB;
	const unsigned int GB = 1024 * MB;
	if (size >= GB)
		return QString("%1 GiB").arg(QString::number(size / float(GB), 'f', 1));
	else if (size >= MB)
		return QString("%1 MiB").arg(QString::number(size / float(MB), 'f', 1));
	else if (size >= KB)
		return QString("%1 KiB").arg(QString::number(size / float(KB), 'f', 1));
	else
		return QString("%1 B").arg(size);
}
