#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"
#include "filecomparator/cfilecomparator.h"
#include "dialogs/csimpleprogressdialog.h"
#include "compiler/compiler_warnings_control.h"

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	CFileComparisonPlugin();

	QString name() const override;

protected:
	void proxySet() override;

private:
	void compareSelectedFiles();

private:
	CFileComparator _comparator;
	CSimpleProgressDialog _progressDialog;
};
