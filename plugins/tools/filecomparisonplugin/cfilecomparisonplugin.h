#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"
#include "filecomparator/cfilecomparator.h"
#include "dialogs/csimpleprogressdialog.h"

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	CFileComparisonPlugin();

	[[nodiscard]] QString name() const override;

protected:
	void proxySet() override;

private:
	void compareSelectedFiles();

private:
	CFileComparator _comparator;
	CSimpleProgressDialog _progressDialog;
};
