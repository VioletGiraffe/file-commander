#pragma once

#include "plugininterface/cfileviewer.h"

class CCsvCommentListModel;
class CCsvTableModel;

class QAction;
class QActionGroup;
class QListView;
class QStatusBar;
class QTableView;

class CCsvViewer final : public CFileViewer
{
public:
	CCsvViewer() noexcept;

protected:
	QString tryLoadFile(const QString& path) override;

private:
	enum class CommentLines { Detect, AsChecked };

	// Re-reads the file: the parser unescapes the text in place, so a delimiter change cannot re-parse what is loaded.
	// Empty on success, otherwise the message for the user.
	QString reload(CommentLines commentLines = CommentLines::AsChecked);
	// Spans, the comment list and the navigation actions all follow the rows the table currently shows
	void tableRowsChanged();
	void goToRow(int row);
	void goToAdjacentComment(bool forward);

private:
	QString _filePath;
	QTableView* _tableView = nullptr;
	CCsvTableModel* _model = nullptr;
	CCsvCommentListModel* _commentListModel = nullptr;
	QListView* _commentList = nullptr;
	QStatusBar* _statusBar = nullptr;
	QAction* _nextCommentAction = nullptr;
	QAction* _previousCommentAction = nullptr;
	QAction* _firstRowIsHeaderAction = nullptr;
	QAction* _commentLinesAction = nullptr;
	QAction* _commentListAction = nullptr;
	bool _commentListVisibilityChosen = false; // Set by the user's toggle: each file then stops deciding it
	QActionGroup* _delimiterGroup = nullptr; // An action's data is its delimiter; empty for auto-detection
};
