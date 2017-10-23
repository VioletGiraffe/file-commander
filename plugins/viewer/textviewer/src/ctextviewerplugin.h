#ifndef CTEXTVIEWERPLUGIN_H
#define CTEXTVIEWERPLUGIN_H

#include "plugininterface/cfilecommanderviewerplugin.h"

class CTextViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CTextViewerPlugin() = default;

	bool canViewFile(const QString& fileName, const QMimeType& type) const override;
	CPluginWindow* viewFile(const QString& fileName) override;
	QString name() const override;
};

#endif // CTEXTVIEWERPLUGIN_H
