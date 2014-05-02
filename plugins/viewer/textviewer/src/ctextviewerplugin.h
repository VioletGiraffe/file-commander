#ifndef CTEXTVIEWERPLUGIN_H
#define CTEXTVIEWERPLUGIN_H

#include "cfilecommanderviewerplugin.h"

extern "C" {
	PLUGIN_EXPORT CFileCommanderPlugin * createPlugin();
}

class CTextViewerPlugin : public CFileCommanderViewerPlugin
{
public:
	CTextViewerPlugin();

	virtual bool canViewCurrentFile() const override;
	virtual QWidget* viewCurrentFile() override;
	virtual QString name() override;
};

#endif // CTEXTVIEWERPLUGIN_H
