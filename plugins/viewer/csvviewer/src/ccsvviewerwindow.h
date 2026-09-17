#pragma once

#include "plugininterface/cpluginwindow.h"

class CCsvCommentListModel;
class CCsvTableModel;

class QAction;
class QActionGroup;
class QDockWidget;
class QTableView;

class CCsvViewerWindow final : public CPluginWindow
{
public:
	explicit CCsvViewerWindow(QWidget* parent = nullptr) noexcept;

	bool loadFile(const QString& filePath);

	void configureForQuickView() override;

private:
	enum class CommentLines { Detect, AsChecked };

	// Re-reads the file: the parser unescapes the text in place, so a delimiter change cannot re-parse what is loaded
	bool reload(CommentLines commentLines = CommentLines::AsChecked);
	// Spans, the comment list and the navigation actions all follow the rows the table currently shows
	void tableRowsChanged();
	void goToRow(int row);
	void goToAdjacentComment(bool forward);
	// In quick view this window is never shown; only the widget knows the window that actually displays it.
	[[nodiscard]] QWidget* dialogParent() const;

private:
	QString _filePath;
	QTableView* _tableView = nullptr;
	CCsvTableModel* _model = nullptr;
	CCsvCommentListModel* _commentListModel = nullptr;
	QDockWidget* _commentDock = nullptr;
	QAction* _nextCommentAction = nullptr;
	QAction* _previousCommentAction = nullptr;
	QAction* _firstRowIsHeaderAction = nullptr;
	QAction* _commentLinesAction = nullptr;
	QAction* _delimiterMenuAction = nullptr;
	bool _commentDockVisibilityDecided = false;
	QActionGroup* _delimiterGroup = nullptr; // An action's data is its delimiter; empty for auto-detection
};
