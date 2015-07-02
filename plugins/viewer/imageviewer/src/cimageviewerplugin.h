#ifndef CIMAGEVIEWERPLUGIN_H
#define CIMAGEVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}

class PLUGIN_EXPORT CImageViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin();

	bool canViewCurrentFile() const override;
	CPluginWindow* viewCurrentFile() override;
	QString name() const override;

private:
	mutable QImage _cachedImage;
	mutable QString _cachedImagePath;
};

#endif // CIMAGEVIEWERPLUGIN_H
