#include "directorycompleter.h"

#include <QDirModel>

DirectoryCompleter::DirectoryCompleter(QObject *parent) :
    QCompleter(parent),
    _home(QDir::homePath()),
    _replaceHome(false)
{
    QDirModel *model = new QDirModel;
    model->setFilter(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot);
    model->setLazyChildCount(true);
    setModel(model);
    setCompletionMode(QCompleter::InlineCompletion);
}

QString DirectoryCompleter::pathFromIndex(const QModelIndex &index) const
{
    QString path = QCompleter::pathFromIndex(index);
    if(_replaceHome && path.startsWith(_home))
        return path.replace(0,_home.size(), "~");
    return QCompleter::pathFromIndex(index);
}

QStringList DirectoryCompleter::splitPath(const QString &path) const
{
    _replaceHome = false;
    if (path.startsWith("~/")) {
        _replaceHome = true;
        return QCompleter::splitPath(QString(path).replace(0, 1, _home));
    }
    return QCompleter::splitPath(path);
}
