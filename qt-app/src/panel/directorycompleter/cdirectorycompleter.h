#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCompleter>
RESTORE_COMPILER_WARNINGS

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
