#ifndef CTEXTVIEWERPLUGIN_H
#define CTEXTVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}

class CTextViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CTextViewerPlugin();

	bool canViewCurrentFile() const override;
	CPluginWindow* viewCurrentFile() override;
	QString name() const override;
};

#endif // CTEXTVIEWERPLUGIN_H
