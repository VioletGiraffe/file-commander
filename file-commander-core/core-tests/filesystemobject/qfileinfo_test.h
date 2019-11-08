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
	QString absoluteFilePath() const;

	QString _absolutePath;
	QString absolutePath() const {return _absolutePath;}

	QString _suffix;
	QString suffix() const {return _suffix;}

	QString _completeSuffix;
	QString completeSuffix() const {return _completeSuffix;}

	QString _baseName;
	QString baseName() const {return _baseName;}

	QString _completeBaseName;
	QString completeBaseName() const {return _completeBaseName;}

	QString _fileName;
	QString fileName() const {return _fileName;}

	bool _exists = false;
	bool exists() const {return _exists;}

	bool _isFile = false;
	bool isFile() const {return _isFile;}

	bool _isDir = false;
	bool isDir() const {return _isDir;}

	bool _isReadable = false;
	bool isReadable() const {return _isReadable;}

	bool _isWritable = false;
	bool isWritable() const {return _isWritable;}

	bool _isHidden = false;
	bool isHidden() const {return _isHidden;}

	bool _isBundle = false;
	bool isBundle() const {return _isBundle;}

	QDateTime _created;
	QDateTime created() const {return _created;}
	QDateTime birthTime() const {return _created;}

	QDateTime _lastModified;
	QDateTime lastModified() const {return _lastModified;}

	qint64 _size = 0;
	qint64 size() const {return	_size;}

	std::map<QFile::Permissions, bool> _permissions;
	bool permission(QFile::Permissions permissions) const {return _permissions.count(permissions) > 0 ? _permissions.at(permissions) : false;}

	bool _isSymLink = false;
	bool isSymLink() const { return _isSymLink; }

	QString _symLinkTarget;
	QString symLinkTarget() const { return _symLinkTarget; }
};
