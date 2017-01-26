#include "cfilecommandertoolplugin.h"

DISABLE_COMPILER_WARNINGS
RESTORE_COMPILER_WARNINGS


CFileCommanderToolPlugin::Command::Command(const QString& name, const QIcon& icon):
	_displayName(name),
	_icon(icon),
	_id((uint32_t) rand()) // The good old rand() should be sufficient - the IDs only have to be unique within each plugin (and std::random cannot be used with just one line of code)
{
}

QString CFileCommanderToolPlugin::Command::name() const
{
	return QObject::tr(_displayName.toUtf8().constData());
}

const QIcon& CFileCommanderToolPlugin::Command::icon() const
{
	return _icon;
}

QString CFileCommanderToolPlugin::Command::id() const
{
	return _id;
}

CFileCommanderPlugin::PluginType CFileCommanderToolPlugin::type()
{
	return Tool;
}

const std::vector<CFileCommanderToolPlugin::Command>& CFileCommanderToolPlugin::commands() const
{
	return _commands;
}
