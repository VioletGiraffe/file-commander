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

	bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	CPluginWindow* viewFile(const QString& fileName) override;
	QString name() const override;
};

#endif // CIMAGEVIEWERPLUGIN_H
