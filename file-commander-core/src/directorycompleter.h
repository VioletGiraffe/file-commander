#pragma once

#include <QCompleter>

class DirectoryCompleter : public QCompleter
{
    Q_OBJECT
public:
    DirectoryCompleter(QObject *parent = nullptr);
    QString pathFromIndex(const QModelIndex &index) const;

public slots:
    QStringList splitPath(const QString &path) const;

private:
    QString _home;
    mutable bool _replaceHome;
};
