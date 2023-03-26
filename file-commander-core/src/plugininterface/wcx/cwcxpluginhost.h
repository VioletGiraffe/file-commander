#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QLibrary>
RESTORE_COMPILER_WARNINGS

#include <deque>

class CWcxPluginHost
{
public:
	CWcxPluginHost();
	void setWcxSearchPath(QString path);

	bool loadPlugin(const QString& path);

private:
	std::deque<QLibrary> _wcxPlugins;
	QString _pluginSearchPath;
};
