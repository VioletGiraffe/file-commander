#ifndef CIMAGEVIEWERPLUGIN_H
#define CIMAGEVIEWERPLUGIN_H

#include "cfilecommanderviewerplugin.h"

extern "C" {
	CFileCommanderPlugin * createPlugin();
}

class PLUGIN_EXPORT CImageViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CImageViewerPlugin();

	virtual bool canViewCurrentFile() const;
	virtual QWidget* viewCurrentFile() const;
};

#endif // CIMAGEVIEWERPLUGIN_H
