#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS

#ifdef CFILESYSTEMOBJECT_TEST
#define QFileInfo QFileInfo_Test
#define QDir QDir_Test

#include <QDir_Test>
#include <QFileInfo_Test>
#else
#include <QDir>
#include <QFileInfo>
#endif

#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

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
	QString parentFolder;
	QString fullPath;
	time_t creationDate = std::numeric_limits<time_t>::max();
	time_t modificationDate = std::numeric_limits<time_t>::max();
	FileSystemObjectType type = UnknownType;
	bool isCdUp = false;
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

	inline explicit CFileSystemObject(const QDir& dir) : CFileSystemObject(QString(dir.absolutePath())) {}

	template <typename T, typename U>
	explicit CFileSystemObject(QStringBuilder<T, U>&& stringBuilder) : CFileSystemObject((QString)std::forward<QStringBuilder<T, U>>(stringBuilder)) {}

	~CFileSystemObject() noexcept = default;

	CFileSystemObject& operator=(CFileSystemObject&&) = default;
	CFileSystemObject& operator=(const CFileSystemObject&) = default;

	CFileSystemObject& operator=(const QString& path);

	void refreshInfo();
	void setPath(const QString& path);

	bool operator==(const CFileSystemObject& other) const;

// Information about this object
	bool isValid() const;

	bool exists() const;
	const CFileSystemObjectProperties& properties() const;
	FileSystemObjectType type() const;
	bool isFile() const;
	bool isDir() const;
	bool isBundle() const;
	bool isEmptyDir() const;
	bool isCdUp() const; // returns true if it's ".." item
	bool isExecutable() const;
	bool isReadable() const;
	// Apparently, it will return false for non-existing files
	bool isWriteable() const;
	bool isHidden() const;

	QString fullAbsolutePath() const;
	QString parentDirPath() const;
	uint64_t size() const;
	uint64_t hash() const;
	const QFileInfo& qFileInfo() const;
	uint64_t rootFileSystemId() const;
	bool isNetworkObject() const;
	bool isSymLink() const;
	QString symLinkTarget() const;

	bool isMovableTo(const CFileSystemObject& dest) const;

	// A hack to store the size of a directory after it's calculated
	void setDirSize(uint64_t size);

	// File name without suffix, or folder name. Same as QFileInfo::completeBaseName.
	QString name() const;
	// Filename + suffix for files, same as name() for folders
	QString fullName() const;
	QString extension() const;
	QString sizeString() const;
	QString modificationDateString() const;

private:
	CFileSystemObjectProperties _properties;
	// Can be used to determine whether two objects are on the same drive
	QFileInfo                   _fileInfo;
	mutable uint64_t            _rootFileSystemId = std::numeric_limits<uint64_t>::max();
};

#undef QFileInfo
#undef QDir
