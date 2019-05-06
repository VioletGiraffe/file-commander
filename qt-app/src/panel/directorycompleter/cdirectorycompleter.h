#pragma once

#include <QCompleter>

class CDirectoryCompleter : public QCompleter
{
public:
	explicit CDirectoryCompleter(QObject *parent);
	QString pathFromIndex(const QModelIndex &index) const override;

public:
	QStringList splitPath(const QString &path) const override;

private:
	const QString _home;
	mutable bool _replaceHome = false;
};
