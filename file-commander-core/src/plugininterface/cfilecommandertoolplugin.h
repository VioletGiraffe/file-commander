#pragma once

#include "cfilecommanderplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

class CFileCommanderToolPlugin : public CFileCommanderPlugin
{
public:

	// A command is an action exported by the plugin for integration into the main File Commander UI
	class Command {
	public:
		Command(const QString& name, const QIcon& icon = QIcon());

		QString name() const;
		const QIcon& icon() const;

		QString id() const;

		bool operator==(const Command& other) const;

	private:
		const QString _displayName; // Translated, if appropriate
		const QIcon _icon;
		const uint32_t _id; // The unique ID for this command
	};

	PluginType type() override;

	const std::vector<Command>& commands() const;

protected:
	std::vector<Command> _commands;
};
