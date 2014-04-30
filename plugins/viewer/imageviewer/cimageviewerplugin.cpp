#include "cimageviewerplugin.h"
#include "QtIncludes.h"

CImageViewerPlugin::CImageViewerPlugin()
{
}

bool CImageViewerPlugin::canViewCurrentFile() const
{
	if (_currentPanel == CFileCommanderPlugin::PluginUnknownPanel)
		return false;
	
	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return false;
	else
		return QImageReader(state->second.currentFolder).canRead();
}

QWidget* CImageViewerPlugin::viewCurrentFile() const
{
	return nullptr;
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}
