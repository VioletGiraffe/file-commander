#pragma once

#include <QDateTime>
#include <QFile>
#include <QString>

#include <map>

struct QFileInfo_Test
{
public:
	QFileInfo_Test() = default;
	QFileInfo_Test(const QString& path) { setFile(path); }

	QString _file;
	void setFile(const QString& file) { _file = file; }

	QString _absoluteFilePath;
	bool _dummyAbsoluteFilePath = true;
	static bool _dummyAbsoluteFilePathGlobal;
	[[nodiscard]] QString absoluteFilePath() const;

	QString _absolutePath;
	[[nodiscard]] QString absolutePath() const {return _absolutePath;}

	QString _suffix;
	[[nodiscard]] QString suffix() const {return _suffix;}

	QString _completeSuffix;
	[[nodiscard]] QString completeSuffix() const {return _completeSuffix;}

	QString _baseName;
	[[nodiscard]] QString baseName() const {return _baseName;}

	QString _completeBaseName;
	[[nodiscard]] QString completeBaseName() const {return _completeBaseName;}

	QString _fileName;
	[[nodiscard]] QString fileName() const {return _fileName;}

	bool _exists = false;
	[[nodiscard]] bool exists() const {return _exists;}

	bool _isFile = false;
	[[nodiscard]] bool isFile() const {return _isFile;}

	bool _isDir = false;
	[[nodiscard]] bool isDir() const {return _isDir;}

	bool _isShortcut = false;
	[[nodiscard]] bool isShortcut() const { return _isShortcut; }

	bool _isReadable = false;
	[[nodiscard]] bool isReadable() const {return _isReadable;}

	bool _isWritable = false;
	[[nodiscard]] bool isWritable() const {return _isWritable;}

	bool _isHidden = false;
	[[nodiscard]] bool isHidden() const {return _isHidden;}

	bool _isBundle = false;
	bool isBundle() const {return _isBundle;}

	QDateTime _created;
	[[nodiscard]] QDateTime created() const {return _created;}
	[[nodiscard]] QDateTime birthTime() const {return _created;}

	QDateTime _lastModified;
	[[nodiscard]] QDateTime lastModified() const {return _lastModified;}

	qint64 _size = 0;
	[[nodiscard]] qint64 size() const {return	_size;}

	std::map<QFile::Permissions, bool> _permissions;
	[[nodiscard]] bool permission(QFile::Permissions permissions) const {return _permissions.count(permissions) > 0 ? _permissions.at(permissions) : false;}

	bool _isSymLink = false;
	[[nodiscard]] bool isSymLink() const { return _isSymLink; }

	QString _symLinkTarget;
	[[nodiscard]] QString symLinkTarget() const { return _symLinkTarget; }
};
