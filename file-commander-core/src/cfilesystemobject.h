#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringBuilder>
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

enum FileSystemObjectType { UnknownType, Directory, File, Bundle };

struct CFileSystemObjectProperties {
	uint64_t size = 0;
	uint64_t hash = 0;
	// Seconds since the epoch; 0 when unknown, e.g. a creation time the filesystem does not record
	time_t creationTime = 0;
	time_t modificationTime = 0;
	QString completeBaseName;
	QString extension;
	QString fullName;
	QString fullPath;
	FileSystemObjectType type = UnknownType;
	bool exists = false;
	// Symlink or junction; a Windows .lnk shortcut is not a link but a regular file
	bool isLink = false;
	// Windows: the hidden attribute, except on a drive root. POSIX: a leading dot, or UF_HIDDEN where the platform has it.
	bool isHidden = false;
	// POSIX: any execute permission bit. Windows: an .exe, .com, .bat or .cmd file.
	bool isExecutable = false;
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

	// Builds the object in memory, deriving the hash from fullPath
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
	[[nodiscard]] const CFileSystemObjectProperties& properties() const;
	[[nodiscard]] FileSystemObjectType type() const;
	[[nodiscard]] bool isFile() const;
	[[nodiscard]] bool isDir() const;
	[[nodiscard]] bool isBundle() const;
	[[nodiscard]] bool isCdUp() const; // returns true if it's ".." item
	[[nodiscard]] bool isExecutable() const;
	[[nodiscard]] bool isHidden() const;

	// Stored strings return by reference, computed ones (parentDirPath) by value.
	// The && overloads return a copy: a reference taken from a temporary object would dangle.
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
	[[nodiscard]] const QString& name() const &;
	[[nodiscard]] QString name() const &&;
	// Filename + suffix for files, same as name() for folders
	[[nodiscard]] const QString& fullName() const &;
	[[nodiscard]] QString fullName() const &&;
	[[nodiscard]] const QString& extension() const &;
	[[nodiscard]] QString extension() const &&;

private:
	// Only called by the constructors, on a default-constructed object. Appends the separator to a directory's fullPath.
	void loadProperties(QString fullPath, QString fullName, const thin_io::directory_entry& entry);

private:
	CFileSystemObjectProperties _properties;
	// Lazily resolved device id of the containing filesystem; identifies which volume the object is on
	mutable uint64_t            _rootFileSystemId = std::numeric_limits<uint64_t>::max();
};
