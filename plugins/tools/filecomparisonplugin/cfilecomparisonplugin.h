#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"
#include "filecomparator/cfilecomparator.h"
#include "dialogs/csimpleprogressdialog.h"

#include <memory>

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	CFileComparisonPlugin() noexcept;

	[[nodiscard]] QString name() const override;

protected:
	void proxySet() override;

private:
	void compareSelectedFiles();

private:
	std::unique_ptr<CSimpleProgressDialog> _progressDialog;
	CFileComparator _comparator; // Must stop its worker before _progressDialog is destroyed.
};
