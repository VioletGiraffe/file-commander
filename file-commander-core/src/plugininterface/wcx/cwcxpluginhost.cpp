#include "cwcxpluginhost.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

CWcxPluginHost::CWcxPluginHost()
{

}

void CWcxPluginHost::setWcxSearchPath(QString path)
{
	_pluginSearchPath = std::move(path);
	QDir pluginDir{ _pluginSearchPath };
	for (auto&& wcxFile: pluginDir.entryInfoList(QStringList{"*.wcx64"}, QDir::Files | QDir::Readable))
	{
		loadPlugin(wcxFile.absoluteFilePath());
	}
}

bool CWcxPluginHost::loadPlugin(const QString& path)
{
	qInfo() << "Loading WCX plugin at:" << path;
	auto& wcx = _wcxPlugins.emplace_back(path);
	if (!wcx.load())
	{
		_wcxPlugins.pop_back();
		assert_and_return_unconditional_r("Failed to load WCX", false);
	}

	if (!wcx.resolve("CanYouHandleThisFileW") /*&& !wcx.resolve("CanYouHandleThisFile")*/)
	{
		wcx.unload();
		_wcxPlugins.pop_back();
		assert_and_return_unconditional_r("Failed to find CanYouHandleThisFile", false);
	}

	return true;
}
