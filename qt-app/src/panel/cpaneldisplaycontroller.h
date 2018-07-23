#pragma once

#include "pluginengine/cpluginengine.h"

class CPluginWindow;
class CPanelWidget;
class QStackedWidget;

class CPanelDisplayController
{
public:
	void setPanelStackedWidget(QStackedWidget* widget);
	void setPanelWidget(CPanelWidget* panelWidget);
	CPanelWidget* panelWidget() const;

	void startQuickView(CPluginEngine::PluginWindowPointerType&& viewerWindow);
	void endQuickView();
	bool quickViewActive() const;

private:
	CPluginEngine::PluginWindowPointerType _quickViewWindow;
	QStackedWidget* _panelStackedWidget = nullptr;
	CPanelWidget* _panelWidget = nullptr;
	bool _quickViewActive = false;
};
