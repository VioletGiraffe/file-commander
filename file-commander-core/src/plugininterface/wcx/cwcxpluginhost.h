#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QLibrary>
RESTORE_COMPILER_WARNINGS

#include <deque>

class CWcxPluginHost
{
public:
	void setWcxSearchPath(QString path);

private:
	bool loadPlugin(const QString& path);

private:
	std::deque<QLibrary> _wcxPlugins; // QLibrary is not movable, can't be stored in a vector
	QString _pluginSearchPath;
};
