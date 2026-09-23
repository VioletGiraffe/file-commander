#include "cfilesystemobject.h"

#include "filesystemhelperfunctions.h"
#include "detail/hashmap_helpers.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "lang/type_traits_fast.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#if defined __linux__ || defined __APPLE__ || defined __FreeBSD__
#include <unistd.h>
#include <sys/stat.h>
#include <wordexp.h>
#elif defined _WIN32
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib") // This lib would have to be added not just to the top level application, but every plugin as well, so using #pragma instead
#endif

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <utility>

static QString expandEnvironmentVariables(const QString& string)
{
#ifdef _WIN32
	if (!string.contains('%'))
		return string;

	WCHAR source[16384 + 1];
	WCHAR result[16384 + 1];

	static_assert (sizeof(WCHAR) == 2);
	const auto length = string.toWCharArray(source);
	source[length] = 0;
	if (const auto resultLength = ExpandEnvironmentStringsW(source, result, static_cast<DWORD>(std::size(result))); resultLength > 0)
		return toPosixSeparators(QString::fromWCharArray(result, (int)resultLength - 1));
	else
		return string;
#else
	QString result = string;
	if (result.startsWith('~'))
		result.replace(0, 1, getenv("HOME"));

	if (result.contains('$'))
	{
		wordexp_t p;
		wordexp(result.toUtf8().constData(), &p, 0);
		const auto w = p.we_wordv;
		if (p.we_wordc > 0)
			result = w[0];

		wordfree(&p);
	}

	return result;
#endif
}


// '/'-separated, without "." and ".." components or duplicate separators; a trailing separator only on a root.
// Not QFileInfo::absoluteFilePath(): on Windows it strips trailing dots and spaces from names.
static QString normalizedAbsolutePath(const QString& path)
{
	QString absolutePath = QDir::cleanPath(QDir{}.absoluteFilePath(path));
#ifdef _WIN32
	// Either spelling of the drive letter must hash to the same object
	if (absolutePath.size() >= 2 && absolutePath[1] == ':')
		absolutePath[0] = absolutePath[0].toUpper();
#endif
	return absolutePath;
}

CFileSystemObject::CFileSystemObject(const QString& path)
{
	if (path.isEmpty())
		return;

	const QString expandedPath = expandEnvironmentVariables(path);
	QString absolutePath = normalizedAbsolutePath(expandedPath);

	if (const auto entry = getDirectoryEntry(absolutePath))
	{
		loadProperties(std::move(absolutePath), *entry);
		// Explorer and Qt ignore the hidden attribute a drive root reports
		if (entry->name.empty())
			_properties.isHidden = false;

		return;
	}

	// Nothing to inspect: the spelling is the only classification
	_properties.fullPath = std::move(absolutePath);
	if (expandedPath.endsWith('/') || expandedPath.endsWith(nativeSeparator()))
	{
		_properties.type = Directory;
		if (!_properties.fullPath.endsWith('/'))
			_properties.fullPath.append('/');
	}

	_properties.hash = pathHash(_properties.fullPath);
	locateNameAndExtension();
}

CFileSystemObject::CFileSystemObject(const QString& parentPath, const thin_io::directory_entry& entry)
{
	assert_debug_only(parentPath.endsWith('/'));

	loadProperties(parentPath % nativeNameToQString(entry.name), entry);
}

uint64_t pathHash(const QString& fullAbsolutePath)
{
	return fullAbsolutePath.isEmpty() ? 0 : QStringHash{}(fullAbsolutePath);
}

CFileSystemObject::CFileSystemObject(CFileSystemObjectProperties properties) : _properties(std::move(properties))
{
	assert_r(_properties.type != Directory || _properties.fullPath.endsWith('/'));
	_properties.hash = pathHash(_properties.fullPath);
	locateNameAndExtension();
}

static QString parentForAbsolutePath(QString absolutePath)
{
	if (absolutePath.endsWith('/'))
		absolutePath.chop(1);

	const auto lastSlash = absolutePath.lastIndexOf('/');
	if (lastSlash < 0)
		return {};

	absolutePath.truncate(lastSlash + 1); // Keep the slash as it signifies a directory rather than a file.
	return absolutePath;
}

CFileSystemObject& CFileSystemObject::operator=(const QString& path)
{
	setPath(path);
	return *this;
}

std::optional<CFileSystemObject> CFileSystemObject::cdUpEntryOf(const QString& dirPath)
{
	if (QDir{ dirPath }.isRoot())
		return {};

	CFileSystemObjectProperties properties;
	properties.fullPath = parentForAbsolutePath(dirPath);
	properties.type = Directory;
	properties.exists = true;
	properties.isCdUp = true;
	return CFileSystemObject{ std::move(properties) };
}

// The first position where a dot can start an extension.
// POSIX: a leading dot belongs to the name, so .bashrc has no extension.
// Windows: .jpg is a JPG file with an empty name, as Windows software treats it.
static constexpr qsizetype FirstExtensionDotIndex =
#ifdef _WIN32
	0;
#else
	1;
#endif

// A directory's trailing separator is not part of its name
[[nodiscard]] static qsizetype nameEndIn(const QString& fullPath) noexcept
{
	return fullPath.endsWith('/') ? fullPath.size() - 1 : fullPath.size();
}

void CFileSystemObject::locateNameAndExtension()
{
	const QStringView fullPath{ _properties.fullPath };
	const qsizetype nameEnd = nameEndIn(_properties.fullPath);
	const qsizetype lastSlash = fullPath.first(nameEnd).lastIndexOf('/');
	// No separator before the name's end: a root, which has no name
	const qsizetype nameStart = lastSlash < 0 ? nameEnd : lastSlash + 1;
	_nameStart = static_cast<uint32_t>(nameStart);
	_extensionDot = static_cast<uint32_t>(nameEnd);
	if (_properties.type == Directory)
		return;

	const QStringView fullName = fullPath.sliced(nameStart, nameEnd - nameStart);
	const qsizetype lastDot = fullName.lastIndexOf('.');
	// A trailing dot starts no extension
	if (lastDot >= FirstExtensionDotIndex && lastDot != fullName.size() - 1)
		_extensionDot = static_cast<uint32_t>(nameStart + lastDot);
}

#ifdef _WIN32
[[nodiscard]] static bool hasExecutableExtension(const QStringView extension)
{
	static constexpr QLatin1StringView ExecutableExtensions[]{ QLatin1StringView{ "exe" }, QLatin1StringView{ "com" }, QLatin1StringView{ "bat" }, QLatin1StringView{ "cmd" } };
	return std::any_of(std::begin(ExecutableExtensions), std::end(ExecutableExtensions), [&extension](const QLatin1StringView executableExtension) {
		return extension.compare(executableExtension, Qt::CaseInsensitive) == 0;
	});
}
#endif

void CFileSystemObject::loadProperties(QString fullPath, const thin_io::directory_entry& entry)
{
	_properties.isLink = isLinkEntry(entry.attributes);
	_properties.exists = true;

	// The target of a live link, otherwise the entry itself: a broken link keeps its own kind and times
	const thin_io::entry_status* const linkTarget = _properties.isLink && entry.link_target ? &*entry.link_target : nullptr;
	const thin_io::entry_status& described = linkTarget ? *linkTarget : entry;

	switch (described.attributes.kind)
	{
	case thin_io::entry_kind::directory:
		_properties.type = Directory;
#ifdef __APPLE__
		if (QFileInfo{ fullPath }.isBundle())
			_properties.type = Bundle;
#endif
		// "dir" and "dir/" must hash the same
		if (!fullPath.endsWith('/'))
			fullPath.append('/');
		break;
	case thin_io::entry_kind::regular_file:
		_properties.type = File;
		break;
	default:
		// A link to anything else, or a broken POSIX link, is shown as a file so delete can unlink it
		_properties.type = _properties.isLink ? File : UnknownType;
		break;
	}

	_properties.fullPath = std::move(fullPath);
	_properties.hash = pathHash(_properties.fullPath);
	locateNameAndExtension();

	_properties.size = _properties.type == File ? described.logical_size.value_or(0) : 0;
	_properties.creationTime = static_cast<time_t>(described.times.creation.seconds);
	_properties.modificationTime = static_cast<time_t>(described.times.last_write.seconds);

#ifdef _WIN32
	_properties.isHidden = entry.attributes.hidden;
	_properties.isExecutable = _properties.type == File && hasExecutableExtension(extension());
#else
	_properties.isHidden = entry.attributes.hidden || fullName().startsWith('.');
	// A broken link's own mode grants nothing
	const bool isBrokenLink = _properties.isLink && !linkTarget;
	_properties.isExecutable = !isBrokenLink && described.permissions && (described.permissions->mode & 0111) != 0;
#endif
}

void CFileSystemObject::setPath(const QString& path)
{
	*this = path.isEmpty() ? CFileSystemObject{} : CFileSystemObject{ path };
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
	return _properties.type == Directory || _properties.type == Bundle;
}

bool CFileSystemObject::isBundle() const
{
	return _properties.type == Bundle;
}

bool CFileSystemObject::isCdUp() const
{
	return _properties.isCdUp;
}

bool CFileSystemObject::isExecutable() const
{
	return _properties.isExecutable;
}

bool CFileSystemObject::isHidden() const
{
	return _properties.isHidden;
}

const QString& CFileSystemObject::fullAbsolutePath() const &
{
	return _properties.fullPath;
}

QString CFileSystemObject::fullAbsolutePath() const &&
{
	return _properties.fullPath;
}

QString CFileSystemObject::parentDirPath() const
{
	const auto parentFolder = parentForAbsolutePath(_properties.fullPath);

	assert_debug_only(parentFolder.endsWith('/') || parentFolder.isEmpty());
	return parentFolder;
}

uint64_t CFileSystemObject::size() const
{
	return _properties.size;
}

uint64_t CFileSystemObject::hash() const
{
	return _properties.hash;
}

uint64_t CFileSystemObject::rootFileSystemId() const
{
	if (_rootFileSystemId == uint64_max)
	{
#ifdef _WIN32
		WCHAR drivePath[32768];
		const auto pathLength = _properties.fullPath.toWCharArray(drivePath);
		drivePath[pathLength] = 0;
		const auto driveNumber = PathGetDriveNumberW(drivePath);
		if (driveNumber != -1)
			_rootFileSystemId = static_cast<uint64_t>(driveNumber);
#else
		// stat() only succeeds on an existing path. If this path doesn't exist yet (e.g. a copy/move destination that will be
		// created), walk up to the nearest existing ancestor: the path will be created on that same device / filesystem.
		QByteArray path = _properties.fullPath.toUtf8();
		struct stat info;
		while (!path.isEmpty())
		{
			if (stat(path.constData(), &info) == 0)
			{
				_rootFileSystemId = static_cast<uint64_t>(info.st_dev);
				break;
			}

			if (errno != ENOENT)
			{
				qInfo() << __FUNCTION__ << "Failed to query device ID for" << _properties.fullPath << strerror(errno);
				break;
			}

			if (path == "/")
				break; // Reached the filesystem root and even it doesn't stat - give up

			const auto lastSlash = path.lastIndexOf('/');
			if (lastSlash < 0)
				break; // No parent component left (shouldn't happen for an absolute path)

			path.truncate(lastSlash == 0 ? 1 : lastSlash); // Keep the leading '/' when the parent is the root itself
		}
#endif
	}

	return _rootFileSystemId;
}

bool CFileSystemObject::isLink() const
{
	return _properties.isLink;
}

time_t CFileSystemObject::creationTime() const
{
	return _properties.creationTime;
}

time_t CFileSystemObject::modificationTime() const
{
	return _properties.modificationTime;
}

// A hack to store the size of a directory after it's calculated
void CFileSystemObject::setDirSize(uint64_t size)
{
	_properties.size = size;
}

QStringView CFileSystemObject::name() const &
{
	if (_properties.isCdUp)
		return u"..";

	return QStringView{ _properties.fullPath }.sliced(_nameStart, _extensionDot - _nameStart);
}

QString CFileSystemObject::name() const &&
{
	return name().toString();
}

// Filename + suffix for files, same as name() for folders
QStringView CFileSystemObject::fullName() const &
{
	if (_properties.isCdUp)
		return u"..";

	return QStringView{ _properties.fullPath }.sliced(_nameStart, nameEndIn(_properties.fullPath) - _nameStart);
}

QString CFileSystemObject::fullName() const &&
{
	return fullName().toString();
}

QStringView CFileSystemObject::extension() const &
{
	const qsizetype nameEnd = nameEndIn(_properties.fullPath);
	if (_extensionDot >= nameEnd)
		return {};

	return QStringView{ _properties.fullPath }.sliced(_extensionDot + 1, nameEnd - _extensionDot - 1);
}

QString CFileSystemObject::extension() const &&
{
	return extension().toString();
}

// Return the list of consecutive full paths leading from the specified target to its root.
// E. g. C:/Users/user/Documents/ -> {C:/Users/user/Documents/, C:/Users/user/, C:/Users/, C:/}
std::vector<QString> pathHierarchy(const QString& path)
{
#ifdef _WIN32
	assert_r(!path.contains('\\')); // A native-separator path leaking in; elsewhere a backslash is a name character
#endif
	assert_r(!path.contains(QStringLiteral("//")) || !QStringView{ path }.right(path.length() - 2).contains(QLatin1String("//")));

	if (path.isEmpty())
		return {};
	else if (path == '/')
		return { path };

	QString pathItem = path.endsWith('/') ? path.left(path.length() - 1) : path;
	std::vector<QString> result{ path };
	while ((pathItem = QFileInfo(pathItem).absolutePath()).length() < result.back().length())
	{
		if (pathItem.endsWith('/'))
			result.emplace_back(pathItem);
		else
			result.emplace_back(pathItem + '/');
	}

	return result;
}
