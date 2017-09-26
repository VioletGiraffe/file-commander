#include "cfilesystemobject.h"
#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"
#include "windows/windowsutils.h"
#include "assert/advanced_assert.h"

#include "fasthash.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <errno.h>

#if defined __linux__ || defined __APPLE__
#include <unistd.h>
#include <sys/stat.h>
#include <wordexp.h>
#elif defined _WIN32
#include "windows/windowsutils.h"

#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib") // This lib would have to be added not just to the top level application, but every plugin as well, so using #pragma instead
#endif

CFileSystemObject::CFileSystemObject(const QFileInfo& fileInfo) : _fileInfo(fileInfo)
{
	refreshInfo();

	if (isDir())
		_dir.setPath(fullAbsolutePath());
}

inline uint64_t hash(const QByteArray& byteArray)
{
	return fasthash64(byteArray.constData(), byteArray.size(), 0);
}

void CFileSystemObject::refreshInfo()
{
	_properties.exists = _fileInfo.exists();
	_properties.fullPath = _fileInfo.absoluteFilePath();

	if (_fileInfo.isFile())
		_properties.type = File;
	else if (_fileInfo.isDir())
	{
		_properties.type = Directory;
		// Normalization - very important for hash calculation and equality checking
		// C:/1/ must be equal to C:/1
		if (!_properties.fullPath.endsWith('/'))
			_properties.fullPath.append('/');
	}
	else if (_properties.exists)
	{
#ifdef _WIN32
		qDebug() << _properties.fullPath << " is neither a file nor a dir";
#endif
	}
	else if (_properties.fullPath.endsWith('/'))
		_properties.type = Directory;

	_properties.hash = ::hash(_properties.fullPath.toUtf8());


	if (_properties.type == File)
	{
		_properties.extension = _fileInfo.suffix();
		_properties.completeBaseName = _fileInfo.completeBaseName();
	}
	else if (_properties.type == Directory)
	{
		_properties.completeBaseName = _fileInfo.baseName();
		const QString suffix = _fileInfo.completeSuffix();
		if (!suffix.isEmpty())
			_properties.completeBaseName = _properties.completeBaseName % '.' % suffix;

		// Ugly temporary bug fix for #141
		if (_properties.completeBaseName.isEmpty() && _properties.fullPath.endsWith('/'))
		{
			const QFileInfo tmpInfo = QFileInfo(_properties.fullPath.left(_properties.fullPath.length() - 1));
			_properties.completeBaseName = tmpInfo.baseName();
			const QString sfx = tmpInfo.completeSuffix();
			if (!sfx.isEmpty())
				_properties.completeBaseName = _properties.completeBaseName % '.' % sfx;
		}
	}

	_properties.fullName = _properties.type == Directory ? _properties.completeBaseName : _fileInfo.fileName();
	_properties.isCdUp = _properties.fullName == QStringLiteral("..");
	// QFileInfo::canonicalPath() / QFileInfo::absolutePath are undefined for non-files
	_properties.parentFolder = _fileInfo.canonicalPath();

	if (!_properties.exists)
		return;

	_properties.creationDate = (time_t) _fileInfo.created().toTime_t();
	_properties.modificationDate = _fileInfo.lastModified().toTime_t();
	_properties.size = _properties.type == File ? _fileInfo.size() : 0;
}

void CFileSystemObject::setPath(const QString& path)
{
	_lastErrorMessage.clear();
	_rootFileSystemId = std::numeric_limits<uint64_t>::max();
	_thisFile.reset();
	_destFile.reset();
	_pos = 0;

	_fileInfo.setFile(path);

	refreshInfo();

	if (isDir())
		_dir.setPath(fullAbsolutePath());
	else
		_dir = QDir();
}

bool CFileSystemObject::operator==(const CFileSystemObject& other) const
{
	return hash() == other.hash();
}


// Information about this object
bool CFileSystemObject::isValid() const
{
	return hash() != 0;
}

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
	return _properties.type;
}

bool CFileSystemObject::isFile() const
{
	return _properties.type == File;
}

bool CFileSystemObject::isDir() const
{
	return _properties.type == Directory;
}

bool CFileSystemObject::isEmptyDir() const
{
	return isDir()? QDir(fullAbsolutePath()).entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty() : false;
}

bool CFileSystemObject::isCdUp() const
{
	return _properties.isCdUp;
}

bool CFileSystemObject::isExecutable() const
{
	return _fileInfo.permission(QFile::ExeUser) || _fileInfo.permission(QFile::ExeOwner) || _fileInfo.permission(QFile::ExeGroup) || _fileInfo.permission(QFile::ExeOther);
}

bool CFileSystemObject::isReadable() const
{
	return _fileInfo.isReadable();
}

// Apparently, it will return false for non-existing files
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
	return isValid() && parent.isValid() && fullAbsolutePath().startsWith(parent.fullAbsolutePath(), Qt::CaseInsensitive);
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

const QDir& CFileSystemObject::qDir() const
{
	return _dir;
}

std::vector<QString> CFileSystemObject::pathHierarchy(const QString& path)
{
	QString pathItem = path.endsWith('/') ? path.left(path.length() - 1) : path;
	std::vector<QString> result(1, path);
	while ((pathItem = QFileInfo(pathItem).absolutePath()).length() < result.back().length())
		result.push_back(pathItem);

	return result;
}

uint64_t CFileSystemObject::rootFileSystemId() const
{
	if (_rootFileSystemId == std::numeric_limits<uint64_t>::max())
	{
#ifdef _WIN32
		const auto driveNumber = PathGetDriveNumberW((WCHAR*) _properties.fullPath.utf16());
		if (driveNumber != -1)
			_rootFileSystemId = (uint64_t) driveNumber;
#else
		struct stat info;
		const int ret = stat(_properties.fullPath.toUtf8().constData(), &info);
		if (ret == 0 || errno == ENOENT)
			_rootFileSystemId = (uint64_t) info.st_dev;
		else
		{
			_lastErrorMessage = strerror(errno);
			qDebug() << __FUNCTION__ << "Failed to query device ID for" << _properties.fullPath;
		}
#endif
	}

	return _rootFileSystemId;
}

bool CFileSystemObject::isNetworkObject() const
{
#ifdef _WIN32
	return _properties.fullPath.startsWith(QStringLiteral("//")) && !_properties.fullPath.startsWith(QStringLiteral("//?/"));
#else
	return false;
#endif
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
	return _properties.type == File ? fileSizeToString(_properties.size) : QString();
}

QString CFileSystemObject::modificationDateString() const
{
	QDateTime modificationDate;
	modificationDate.setTime_t((uint)_properties.modificationDate);
	modificationDate = modificationDate.toLocalTime();
	return modificationDate.toString(QStringLiteral("dd.MM.yyyy hh:mm"));
}

// Operations
FileOperationResultCode CFileSystemObject::copyAtomically(const QString& destFolder, const QString& newName)
{
	assert_r(isFile());
	assert_r(QFileInfo(destFolder).isDir());

	QFile file (_properties.fullPath);
	const bool succ = file.copy(destFolder + (newName.isEmpty() ? _properties.fullName : newName));
	if (!succ)
		_lastErrorMessage = file.errorString();
	return succ ? rcOk : rcFail;
}

FileOperationResultCode CFileSystemObject::moveAtomically(const QString& location, const QString& newName)
{
	if (!exists())
		return rcObjectDoesntExist;
	else if (isCdUp())
		return rcFail;

	assert_r(QFileInfo(location).isDir());
	const QString fullNewName = location % '/' % (newName.isEmpty() ? _properties.fullName : newName);
	const QFileInfo destInfo(fullNewName);
	const bool newNameDiffersOnlyByCase = destInfo.absoluteFilePath().compare(fullAbsolutePath(), Qt::CaseInsensitive) == 0;
	if (destInfo.exists() && (isDir() || destInfo.isFile()))
		// If the file system is case-insensitive, and the source and destination only differ by case, renaming is allowed even though formally the destination already exists (fix for #102)
		if (caseSensitiveFilesystem() || !newNameDiffersOnlyByCase)
			return rcTargetAlreadyExists;
	
	// Special case for Windows, where QFile::rename and QDir::rename fail to handle names that only differ by letter case (https://bugreports.qt.io/browse/QTBUG-3570)
#ifdef _WIN32
	if (newNameDiffersOnlyByCase)
	{
		if (MoveFileW((WCHAR*)fullAbsolutePath().utf16(), (WCHAR*)destInfo.absoluteFilePath().utf16()) != 0)
			return rcOk;

		_lastErrorMessage = ErrorStringFromLastError();
		return rcFail;
	}
#endif

	if (isFile())
	{
		QFile file(_properties.fullPath);
		const bool succ = file.rename(fullNewName);
		if (!succ)
		{
			_lastErrorMessage = file.errorString();
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
FileOperationResultCode CFileSystemObject::copyChunk(size_t chunkSize, const QString& destFolder, const QString& newName /*= QString()*/)
{
	assert_r(bool(_thisFile) == bool(_destFile));
	assert_r(isFile());
	assert_r(QFileInfo(destFolder).isDir());

	if (!copyOperationInProgress())
	{
		_pos = 0;

		// Creating files
		_thisFile = std::make_shared<QFile>(fullAbsolutePath());
		_destFile = std::make_shared<QFile>(destFolder + (newName.isEmpty() ? _properties.fullName : newName));

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

		_destFile->resize(size());
	}

	assert_r(_destFile->isOpen() == _thisFile->isOpen());

	const auto actualChunkSize = std::min(chunkSize, (size_t)(size() - _pos));

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

FileOperationResultCode CFileSystemObject::moveChunk(uint64_t /*chunkSize*/, const QString &destFolder, const QString& newName)
{
	return moveAtomically(destFolder, newName);
}

bool CFileSystemObject::copyOperationInProgress() const
{
	if (!_destFile && !_thisFile)
		return false;

	assert_r(_destFile->isOpen() == _thisFile->isOpen());
	return _destFile->isOpen() && _thisFile->isOpen();
}

uint64_t CFileSystemObject::bytesCopied() const
{
	return _pos;
}

FileOperationResultCode CFileSystemObject::cancelCopy()
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

bool CFileSystemObject::makeWritable(bool writeable)
{
	assert_and_return_message_r(isFile(), "This method only works for files", false);

#ifdef _WIN32
	const QString UNCPath = toUncPath(fullAbsolutePath());
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

FileOperationResultCode CFileSystemObject::remove()
{
	qDebug() << "Removing" << _properties.fullPath;

	assert_and_return_message_r(_fileInfo.exists(), "Object doesn't exist", rcObjectDoesntExist);

	if (isFile())
	{
		QFile file(_properties.fullPath);
		if (file.remove())
			return rcOk;
		else
		{
			_lastErrorMessage = file.errorString();
			return rcFail;
		}
	}
	else if (isDir())
	{
		QDir dir (_properties.fullPath);
		assert_r(dir.entryList(QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty());
		errno = 0;
		if (!dir.rmdir("."))
		{
#if defined __linux || defined __APPLE__
//			dir.cdUp();
//			bool succ = dir.remove(_fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1));
//			qDebug() << "Removing " << _fileInfo.absoluteFilePath().mid(_fileInfo.absoluteFilePath().lastIndexOf("/") + 1) << "from" << dir.absolutePath();
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

QString CFileSystemObject::lastErrorMessage() const
{
	return _lastErrorMessage;
}

QString CFileSystemObject::expandEnvironmentVariables(const QString& string)
{
#ifdef _WIN32
	if (!string.contains('%'))
		return string;

	static WCHAR result[16384 + 1];
	if (ExpandEnvironmentStringsW((WCHAR*)string.utf16(), result, sizeof(result) / sizeof(result[0])) != 0)
		return toPosixSeparators(QString::fromUtf16((char16_t*)result));
	else
		return string;
#else
	if (!string.contains('$'))
		return string;

	wordexp_t p;
	wordexp("$HOME/bin", &p, 0);
	const auto w = p.we_wordv;
	const QString result = p.we_wordc > 0 ? w[0] : string;
	wordfree(&p);

	return result;
#endif
}


DirectoryHierarchy enumerateDirectoryRecursively(const CFileSystemObject& root, const std::function<void (QString)>& observer, const std::atomic<bool>& abort)
{
	if (observer)
		observer(root.fullAbsolutePath());

	DirectoryHierarchy hierarchy;
	hierarchy.rootItem = root;

	if (abort || !root.isDir())
		return hierarchy;

	const auto list = root.qDir().entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::System);
	for (const auto& item: list)
		hierarchy.subitems.emplace_back(enumerateDirectoryRecursively(CFileSystemObject(item), observer, abort));

	return hierarchy;
}

void flattenHierarchy(const DirectoryHierarchy& hierarchy, FlattenedHierarchy& result)
{
	if (hierarchy.rootItem.isDir())
	{
		result.directories.push_back(hierarchy.rootItem);
		for (const DirectoryHierarchy& subitem: hierarchy.subitems)
			flattenHierarchy(subitem, result);
	}
	else
		result.files.push_back(hierarchy.rootItem);
}

FlattenedHierarchy flattenHierarchy(const DirectoryHierarchy& hierarchy)
{
	FlattenedHierarchy result;
	flattenHierarchy(hierarchy, result);
	return result;
}


FlattenedHierarchy flattenHierarchy(const std::vector<DirectoryHierarchy>& hierarchy)
{
	FlattenedHierarchy result;
	for (const auto h: hierarchy)
		flattenHierarchy(h, result);
	return result;
}
