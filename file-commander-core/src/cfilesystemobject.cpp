#include "cfilesystemobject.h"
#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"
#include "windows/windowsutils.h"
#include "assert/advanced_assert.h"

#include "fasthash.h"

#ifdef CFILESYSTEMOBJECT_TEST
#define QFileInfo QFileInfo_Test
#define QDir QDir_Test
#endif

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

static inline QString expandEnvironmentVariables(const QString& string)
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


CFileSystemObject::CFileSystemObject(const QFileInfo& fileInfo) : _fileInfo(fileInfo)
{
	refreshInfo();
}

CFileSystemObject::CFileSystemObject(const QString& path) : _fileInfo(expandEnvironmentVariables(path))
{
	refreshInfo();
}

inline uint64_t hash(const QByteArray& byteArray)
{
	return fasthash64(byteArray.constData(), byteArray.size(), 0);
}

inline QString parentForAbsolutePath(QString absolutePath)
{
	if (absolutePath.endsWith('/'))
		absolutePath.chop(1);

	const int lastSlash = absolutePath.lastIndexOf('/');
	if (lastSlash <= 0)
		return QString();

	absolutePath.truncate(lastSlash + 1); // Keep the slash as it signifies a directory rather than a file.
	return absolutePath;
}

CFileSystemObject & CFileSystemObject::operator=(const QString & path)
{
	setPath(path);
	return *this;
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
		qInfo() << _properties.fullPath << " is neither a file nor a dir";
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
	_properties.isCdUp = _properties.fullName == QLatin1String("..");
	// QFileInfo::canonicalPath() / QFileInfo::absolutePath are undefined for non-files
	_properties.parentFolder = parentForAbsolutePath(_properties.fullPath);

	if (!_properties.exists)
		return;

	_properties.creationDate = (time_t) _fileInfo.created().toTime_t();
	_properties.modificationDate = _fileInfo.lastModified().toTime_t();
	_properties.size = _properties.type == File ? _fileInfo.size() : 0;

	if (isDir())
		_dir.setPath(fullAbsolutePath());
	else
		_dir = QDir();
}

void CFileSystemObject::setPath(const QString& path)
{
	if (path.isEmpty())
	{
		*this = CFileSystemObject();
		return;
	}

	_rootFileSystemId = std::numeric_limits<uint64_t>::max();

	_fileInfo.setFile(expandEnvironmentVariables(path));

	refreshInfo();
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
	if (path.isEmpty())
		return {};

	QString pathItem = path.endsWith('/') ? path.left(path.length() - 1) : path;
	std::vector<QString> result {path == '/' ? QString() : path};
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
			qInfo() << __FUNCTION__ << "Failed to query device ID for" << _properties.fullPath;
			qInfo() << strerror(errno);
		}
#endif
	}

	return _rootFileSystemId;
}

bool CFileSystemObject::isNetworkObject() const
{
#ifdef _WIN32
	return _properties.fullPath.startsWith(QLatin1String("//")) && !_properties.fullPath.startsWith(QLatin1String("//?/"));
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
	return modificationDate.toString(QLatin1String("dd.MM.yyyy hh:mm"));
}
