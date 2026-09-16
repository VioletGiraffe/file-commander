#pragma once

#include "plugininterface/cpluginwindow.h"

class CCsvTableModel;

class QAction;
class QActionGroup;
class QTableView;

class CCsvViewerWindow final : public CPluginWindow
{
public:
	explicit CCsvViewerWindow(QWidget* parent = nullptr) noexcept;

	bool loadFile(const QString& filePath);

	void configureForQuickView() override;

private:
	// Re-reads the file: the parser unescapes the text in place, so a delimiter change cannot re-parse what is loaded
	bool reload();
	// In quick view this window is never shown; only the widget knows the window that actually displays it.
	[[nodiscard]] QWidget* dialogParent() const;

private:
	QString _filePath;
	QTableView* _tableView = nullptr;
	CCsvTableModel* _model = nullptr;
	QAction* _firstRowIsHeaderAction = nullptr;
	QAction* _delimiterMenuAction = nullptr;
	QActionGroup* _delimiterGroup = nullptr; // An action's data is its delimiter; empty for auto-detection
};
