#ifndef CIMAGEVIEWERPLUGIN_H
#define CIMAGEVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}

class PLUGIN_EXPORT CImageViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin();

	virtual bool canViewCurrentFile() const override;
	virtual QWidget* viewCurrentFile() override;
	virtual QString name() override;
};

#endif // CIMAGEVIEWERPLUGIN_H
