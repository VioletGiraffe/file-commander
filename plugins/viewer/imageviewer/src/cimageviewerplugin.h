#ifndef CIMAGEVIEWERPLUGIN_H
#define CIMAGEVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

class CImageViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin() = default;

	bool canViewCurrentFile() const override;
	CPluginWindow* viewCurrentFile() override;
	QString name() const override;

private:
	mutable QImage _cachedImage;
	mutable QString _cachedImagePath;
};

#endif // CIMAGEVIEWERPLUGIN_H
