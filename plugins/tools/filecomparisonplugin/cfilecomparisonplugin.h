#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"
#include "cfilecomparator.h"

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
};
