#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"
#include "filecomparator/cfilecomparator.h"

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	QString name() const override;

protected:
	void proxySet() override;

private:
	void compareSelectedFiles();

private:
	CFileComparator _comparator;
};
