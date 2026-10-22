#pragma once

#include "plugininterface/cfilecommanderviewerplugin.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

class CFileViewer;
class CPanelWidget;
class QStackedWidget;

class CPanelDisplayController
{
public:
	void setPanelStackedWidget(QStackedWidget* widget);
	void setPanelWidget(CPanelWidget* panelWidget);
	[[nodiscard]] CPanelWidget* panelWidget() const;

	// Loads the file into the viewer in place when the same plugin made it, otherwise replaces the viewer
	void showQuickView(CFileCommanderViewerPlugin& plugin, const QString& filePath);
	void endQuickView();
	[[nodiscard]] bool quickViewActive() const { return _quickViewer != nullptr; }
	// Null while quick view is off
	[[nodiscard]] CFileViewer* quickViewer() const { return _quickViewer; }
	[[nodiscard]] const QString& quickViewFilePath() const { return _quickViewFilePath; }

private:
	QStackedWidget* _panelStackedWidget = nullptr;
	CPanelWidget* _panelWidget = nullptr;
	CFileCommanderViewerPlugin* _quickViewPlugin = nullptr; // The plugin that created _quickViewer
	CFileViewer* _quickViewer = nullptr; // Owned by _panelStackedWidget
	QString _quickViewFilePath;
};
