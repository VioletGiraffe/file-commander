#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"

class CPluginWindow;
class CPanelWidget;
class QStackedWidget;

class CPanelDisplayController
{
public:
	void setPanelStackedWidget(QStackedWidget* widget);
	void setPanelWidget(CPanelWidget* panelWidget);
	[[nodiscard]] CPanelWidget* panelWidget() const;

	void startQuickView(CFileCommanderViewerPlugin::WindowPtr<CPluginWindow>&& viewerWindow);
	void endQuickView();
	[[nodiscard]] bool quickViewActive() const;

private:
	QStackedWidget* _panelStackedWidget = nullptr;
	CPanelWidget* _panelWidget = nullptr;
	CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> _quickViewWindow;
	bool _quickViewActive = false;
};
