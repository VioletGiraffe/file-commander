#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringBuilder>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <limits>
#include <optional>
#include <stdint.h>
#include <time.h>
#include <vector>

namespace thin_io { struct directory_entry; }

// Return the list of consecutive full paths leading from the specified target to its root.
// E. g. C:/Users/user/Documents/ -> {C:/Users/user/Documents/, C:/Users/user/, C:/Users/, C:/}
std::vector<QString> pathHierarchy(const QString& path);

// CFileSystemObject::hash() of the object with this fullAbsolutePath()
// An empty path hashes to 0, so every empty object does: simpler for the callers
[[nodiscard]] uint64_t pathHash(const QString& fullAbsolutePath);

enum FileSystemObjectType : uint8_t { UnknownType, Directory, File, Bundle };

// Field order: what sorting and lookups read comes first; the one-byte fields share one 8-byte slot
struct CFileSystemObjectProperties {
	QString fullPath;
	FileSystemObjectType type = UnknownType;
	bool exists = false;
	// Symlink or junction; a Windows .lnk shortcut is not a link but a regular file
	bool isLink = false;
	// Windows: the hidden attribute, except on a drive root. POSIX: a leading dot, or UF_HIDDEN where the platform has it.
	bool isHidden = false;
	// POSIX: any execute permission bit. Windows: an .exe, .com, .bat or .cmd file.
	bool isExecutable = false;
	// The [..] entry, named ".."; fullPath is the parent folder's
	bool isCdUp = false;
	uint64_t size = 0;
	uint64_t hash = 0;
	// Seconds since the epoch; 0 when unknown, e.g. a creation time the filesystem does not record
	time_t modificationTime = 0;
	time_t creationTime = 0;
};

class CFileSystemObject
{
public:
	CFileSystemObject() = default;
	CFileSystemObject(CFileSystemObject&&) noexcept = default;
	CFileSystemObject(const CFileSystemObject&) = default;

	// Expands environment variables and resolves a relative path against the current directory
	explicit CFileSystemObject(const QString& path);
	// A listed child of parentPath, which ends with a separator
	CFileSystemObject(const QString& parentPath, const thin_io::directory_entry& entry);

	// Builds the object in memory, deriving the hash and the name split from fullPath
	explicit CFileSystemObject(CFileSystemObjectProperties properties);

	// The [..] entry of dirPath's listing: the parent folder. Empty for a root.
	[[nodiscard]] static std::optional<CFileSystemObject> cdUpEntryOf(const QString& dirPath);

	template <typename T, typename U>
	explicit CFileSystemObject(QStringBuilder<T, U>&& stringBuilder) : CFileSystemObject((QString)std::forward<QStringBuilder<T, U>>(stringBuilder)) {}

	~CFileSystemObject() noexcept = default;

	CFileSystemObject& operator=(CFileSystemObject&&) = default;
	CFileSystemObject& operator=(const CFileSystemObject&) = default;

	CFileSystemObject& operator=(const QString& path);

	void setPath(const QString& path);

	[[nodiscard]] bool operator==(const CFileSystemObject& other) const;

// Information about this object
	[[nodiscard]] bool isValid() const;

	[[nodiscard]] bool exists() const;
	[[nodiscard]] FileSystemObjectType type() const;
	[[nodiscard]] bool isFile() const;
	[[nodiscard]] bool isDir() const;
	[[nodiscard]] bool isBundle() const;
	[[nodiscard]] bool isCdUp() const; // returns true if it's ".." item
	[[nodiscard]] bool isExecutable() const;
	[[nodiscard]] bool isHidden() const;

	// fullAbsolutePath() returns by reference, the names as views into it, parentDirPath() by value.
	// The && overloads return a copy: a reference or a view taken from a temporary object would dangle.
	[[nodiscard]] const QString& fullAbsolutePath() const &;
	[[nodiscard]] QString fullAbsolutePath() const &&;
	[[nodiscard]] QString parentDirPath() const;
	[[nodiscard]] uint64_t size() const;
	[[nodiscard]] uint64_t hash() const;
	[[nodiscard]] uint64_t rootFileSystemId() const;
	[[nodiscard]] bool isLink() const;

	[[nodiscard]] time_t creationTime() const;
	[[nodiscard]] time_t modificationTime() const;

	// A hack to store the size of a directory after it's calculated
	void setDirSize(uint64_t size);

	// File name without its extension, or folder name
	[[nodiscard]] QStringView name() const &;
	[[nodiscard]] QString name() const &&;
	// Filename + suffix for files, same as name() for folders
	[[nodiscard]] QStringView fullName() const &;
	[[nodiscard]] QString fullName() const &&;
	[[nodiscard]] QStringView extension() const &;
	[[nodiscard]] QString extension() const &&;

private:
	// Only called by the constructors, on a default-constructed object. Appends the separator to a directory's fullPath.
	void loadProperties(QString fullPath, const thin_io::directory_entry& entry);
	// Sets _nameStart and _extensionDot; requires the final fullPath and type
	void locateNameAndExtension();

private:
	// Member order: what the name getters, sorting and lookups read fits in the first 64 bytes
	// The name runs from _nameStart to the end of fullPath, less a directory's trailing separator
	uint32_t                    _nameStart = 0;
	// The dot before the extension; the name's end when there is no extension
	uint32_t                    _extensionDot = 0;
	CFileSystemObjectProperties _properties;
	// Lazily resolved device id of the containing filesystem; identifies which volume the object is on
	mutable uint64_t            _rootFileSystemId = std::numeric_limits<uint64_t>::max();
};
