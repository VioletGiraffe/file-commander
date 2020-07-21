#include "cdirectorycompleter.h"

#include <QFileSystemModel>

CDirectoryCompleter::CDirectoryCompleter(QObject *parent) :
	QCompleter(parent),
	_home(QDir::homePath()) // TODO: use CFileSystemObject?
{
	auto *model = new QFileSystemModel(this);
	model->setFilter(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot);
	setModel(model);
	setCompletionMode(QCompleter::InlineCompletion);
}

QString CDirectoryCompleter::pathFromIndex(const QModelIndex &index) const
{
	QString path = QCompleter::pathFromIndex(index);
	if (_replaceHome && path.startsWith(_home)) // TODO: use CFileSystemObject facilities instead?
		return path.replace(0, _home.size(), "~");

	return path;
}

QStringList CDirectoryCompleter::splitPath(const QString &path) const
{
	_replaceHome = false;
	if (path.startsWith("~/"))
	{
		_replaceHome = true;
		return QCompleter::splitPath(QString(path).replace(0, 1, _home));
	}
	return QCompleter::splitPath(path);
}
