#ifndef CFILESYSTEMOBJECT_H
#define CFILESYSTEMOBJECT_H

#include <stdint.h>
#include <vector>
#include <memory>
#include "QtCoreIncludes"
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QString>
#include <QStringBuilder>

#include "fileoperationresultcode.h"

enum FileSystemObjectType { UnknownType, Directory, File };

struct CFileSystemObjectProperties {
	QString  completeBaseName;
	QString  extension;
	QString  fullName;
	QString  parentFolder;
	QString  fullPath;
	FileSystemObjectType type = UnknownType;
	uint64_t size = 0;
	time_t   creationDate = std::numeric_limits<time_t>::max();
	time_t   modificationDate = std::numeric_limits<time_t>::max();
	qulonglong hash = 0;
	bool exists = false;
};

class QIcon;
class CFileSystemObject
{
public:
	explicit CFileSystemObject(const QFileInfo & fileInfo);

	inline CFileSystemObject() {}
	inline explicit CFileSystemObject(const QString& path) : CFileSystemObject(QFileInfo(path)) {}
	inline explicit CFileSystemObject(const QDir& dir) : CFileSystemObject(QFileInfo(dir.absolutePath())) {}

	template <typename T, typename U>
	explicit CFileSystemObject(QStringBuilder<T, U>&& stringBuilder) : CFileSystemObject(QString(stringBuilder))
	{
	}

	~CFileSystemObject();

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
	bool isEmptyDir() const;
	bool isCdUp() const; // returns true if it's ".." item
	bool isExecutable() const;
	bool isReadable() const;
	// Apparently, it will return false for non-existing files
	bool isWriteable() const;
	bool isHidden() const;
	// Returns true if this object is a child of parent, either direct or indirect
	bool isChildOf(const CFileSystemObject& parent) const;
	QString fullAbsolutePath() const;
	QString parentDirPath() const;
	const QIcon& icon() const;
	uint64_t size() const;
	qulonglong hash() const;
	const QFileInfo& qFileInfo() const;
	const QDir& qDir() const;
	static std::vector<QString> pathHierarchy(const QString& path);
	uint64_t rootFileSystemId() const;

	bool isMovableTo(const CFileSystemObject& dest) const;

	// A hack to store the size of a directory after it's calculated
	void setDirSize(uint64_t size);

	// File name without suffix, or folder name
	QString name() const;
	// Filename + suffix for files, same as name() for folders
	QString fullName() const;
	QString extension() const;
	QString sizeString() const;
	QString modificationDateString() const;

// Operations
	FileOperationResultCode copyAtomically(const QString& destFolder, const QString& newName = QString());
	FileOperationResultCode moveAtomically(const QString& destFolder, const QString& newName = QString());

// Non-blocking file copy API
	// Requests copying the next (or the first if copyOperationInProgress() returns false) chunk of the file.
	FileOperationResultCode copyChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString());
	FileOperationResultCode moveChunk(uint64_t chunkSize, const QString& destFolder, const QString& newName = QString());
	bool copyOperationInProgress() const;
	uint64_t bytesCopied() const;
	FileOperationResultCode cancelCopy();

	bool                    makeWritable(bool writeable = true);
	FileOperationResultCode remove();

	QString lastErrorMessage() const;

private:
	QFileInfo                   _fileInfo;
	QDir                        _dir;
	CFileSystemObjectProperties _properties;
	mutable QString             _lastError;
	// Can be used to determine whether 2 objects are on the same drive
	mutable uint64_t            _rootFileSystemId = std::numeric_limits<uint64_t>::max();

// For copying / moving
	std::shared_ptr<QFile>      _thisFile;
	std::shared_ptr<QFile>      _destFile;
	uint64_t                    _pos = 0;
};

#endif // CFILESYSTEMOBJECT_H
