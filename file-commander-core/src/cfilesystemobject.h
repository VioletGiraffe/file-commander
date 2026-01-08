#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS

#ifdef CFILESYSTEMOBJECT_TEST
#define QFileInfo QFileInfo_Test
#define QDir QDir_Test

#include <QFileInfo_Test>
#else
#include <QFileInfo>
#endif

#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

class QDir;

// Return the list of consecutive full paths leading from the specified target to its root.
// E. g. C:/Users/user/Documents/ -> {C:/Users/user/Documents/, C:/Users/user/, C:/Users/, C:/}
std::vector<QString> pathHierarchy(const QString& path); // Keeping this function here because it's covered by a CFileSystemObject test and needs the QFileInfo_Test include

enum FileSystemObjectType { UnknownType, Directory, File, Bundle };

struct CFileSystemObjectProperties {
	uint64_t size = 0;
	uint64_t hash = 0;
	QString completeBaseName;
	QString extension;
	QString fullName;
	QString fullPath;
	FileSystemObjectType type = UnknownType;
	bool exists = false;
};

class CFileSystemObject
{
public:
	CFileSystemObject() = default;
	CFileSystemObject(CFileSystemObject&&) noexcept = default;
	CFileSystemObject(const CFileSystemObject&) = default;

	explicit CFileSystemObject(const QFileInfo & fileInfo);
	explicit CFileSystemObject(const QString& path);

	explicit CFileSystemObject(const QDir& dir);

	template <typename T, typename U>
	explicit CFileSystemObject(QStringBuilder<T, U>&& stringBuilder) : CFileSystemObject((QString)std::forward<QStringBuilder<T, U>>(stringBuilder)) {}

	~CFileSystemObject() noexcept = default;

	CFileSystemObject& operator=(CFileSystemObject&&) = default;
	CFileSystemObject& operator=(const CFileSystemObject&) = default;

	CFileSystemObject& operator=(const QString& path);

	void refreshInfo();
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
	[[nodiscard]] bool isEmptyDir() const;
	[[nodiscard]] bool isCdUp() const; // returns true if it's ".." item
	[[nodiscard]] bool isExecutable() const;
	[[nodiscard]] bool isReadable() const;
	// Apparently, it will return false for non-existing files
	[[nodiscard]] bool isWriteable() const;
	[[nodiscard]] bool isHidden() const;

	[[nodiscard]] QString fullAbsolutePath() const;
	[[nodiscard]] QString parentDirPath() const;
	[[nodiscard]] uint64_t size() const;
	[[nodiscard]] uint64_t hash() const;
	[[nodiscard]] const QFileInfo& qFileInfo() const;
	[[nodiscard]] uint64_t rootFileSystemId() const;
	[[nodiscard]] bool isNetworkObject() const;
	[[nodiscard]] bool isSymLink() const;
	[[nodiscard]] QString symLinkTarget() const;

	[[nodiscard]] time_t creationTime() const;
	[[nodiscard]] time_t modificationTime() const;

	[[nodiscard]] bool isMovableTo(const CFileSystemObject& dest) const;

	// A hack to store the size of a directory after it's calculated
	void setDirSize(uint64_t size);

	// File name without suffix, or folder name. Same as QFileInfo::completeBaseName.
	[[nodiscard]] QString name() const;
	// Filename + suffix for files, same as name() for folders
	[[nodiscard]] QString fullName() const;
	[[nodiscard]] QString extension() const;

private:
	CFileSystemObjectProperties _properties;

	static constexpr auto invalid_time = std::numeric_limits<time_t>::max();
	mutable time_t _creationDate = invalid_time;
	mutable time_t _modificationDate = invalid_time;
	// Can be used to determine whether two objects are on the same drive
	QFileInfo                   _fileInfo;
	mutable uint64_t            _rootFileSystemId = std::numeric_limits<uint64_t>::max();
};

#undef QFileInfo
#undef QDir
