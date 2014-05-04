#ifndef CIMAGEVIEWERPLUGIN_H
#define CIMAGEVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

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

#ifdef _WIN32
#pragma warning(pop)
#endif

#endif // CIMAGEVIEWERPLUGIN_H
