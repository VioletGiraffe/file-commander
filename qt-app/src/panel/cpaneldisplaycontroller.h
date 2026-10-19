#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

class CPluginWindow;
class CPanelWidget;
class QStackedWidget;

class CPanelDisplayController
{
public:
	void setPanelStackedWidget(QStackedWidget* widget);
	void setPanelWidget(CPanelWidget* panelWidget);
	[[nodiscard]] CPanelWidget* panelWidget() const;

	void startQuickView(CFileCommanderViewerPlugin::WindowPtr<CPluginWindow>&& viewerWindow, QString filePath);
	void endQuickView();
	[[nodiscard]] bool quickViewActive() const;
	// Matches each path itself and, for a folder, everything under it.
	void closeQuickViewedFileIfWithin(const QStringList& paths);

private:
	QStackedWidget* _panelStackedWidget = nullptr;
	CPanelWidget* _panelWidget = nullptr;
	CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> _quickViewWindow;
	QString _quickViewFilePath;
	bool _quickViewActive = false;
};
