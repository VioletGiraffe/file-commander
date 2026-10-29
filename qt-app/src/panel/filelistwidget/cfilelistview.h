#pragma once

#include "panel/columns.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QPersistentModelIndex>
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

// Qt signals/slots system doesn't apply here because there should be a list of observers, and the signal shall not go further once it's been consumed by a listener

struct FileListViewEventObserver {
	virtual ~FileListViewEventObserver() = default;

	virtual bool fileListReturnPressed() = 0;
	virtual bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& index) = 0;
};

struct FileListReturnPressedObserver : FileListViewEventObserver {
	inline bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex&) override { return false; }
};

struct FileListReturnPressOrDoubleClickObserver : FileListViewEventObserver {
	inline bool fileListReturnPressed() override { return false; }
};

class QMouseEvent;
class QFocusEvent;
class QHeaderView;
class CFileListView final : public QTreeView
{
	Q_OBJECT

public:
	explicit CFileListView(QWidget *parent = nullptr);
	void addEventObserver(FileListViewEventObserver* observer);

	// Column widths and order, independent of the view's width
	struct ColumnLayout
	{
		std::array<float, NumberOfColumns> widthShares = { 0.52f, 0.135f, 0.143f, 0.2f }; // By logical column; need not sum to 1
		std::array<int, NumberOfColumns> visualOrder = { NameColumn, ExtColumn, SizeColumn, DateColumn }; // The logical column at each visual position
	};
	// Empty unless the header has every column and a non-zero total width
	[[nodiscard]] static std::optional<ColumnLayout> columnLayoutOf(const QHeaderView& header);
	[[nodiscard]] const ColumnLayout& columnLayout() const;
	// Requires a model: the header has no sections before. The column widths follow the header's width from here on.
	void applyColumnLayout(const ColumnLayout& layout);

	// Preserves item's selection state
	void moveCursorToItem(const QModelIndex &index, bool invertSelection = false);

	void invertSelection();

	void modelAboutToBeReset();

	[[nodiscard]] bool editingInProgress() const;

	struct ScrollPosition
	{
		// The rows on screen, top to bottom, with the viewport y of each
		std::vector<std::pair<QPersistentModelIndex, int>> visibleRows;
	};
	[[nodiscard]] ScrollPosition scrollPosition() const;
	// Scrolls the first of the rows that is still in the model back to where it was on screen. False if none is: a model reset leaves none.
	bool restoreScrollPosition(const ScrollPosition& position);

	// Logs the time from start to the end of the next paint if it reaches 100 ms
	void logTimeToNextPaint(std::chrono::steady_clock::time_point start);

signals:
	void contextMenuRequested();
	void ctrlEnterPressed();
	void ctrlShiftEnterPressed();
	void itemMiddleClicked(const QModelIndex& index);
	void keyPressed(QString keyText, int key, Qt::KeyboardModifiers modifiers);
	void shiftStateChanged(bool shiftPressed);

protected:
	// For controlling selection
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	// For showing context menu and managing single click actions
	void mouseReleaseEvent(QMouseEvent *event) override;
	// For managing selection and cursor
	void keyPressEvent(QKeyEvent * event) override;
	void keyReleaseEvent(QKeyEvent * event) override;
	// Keep the bottom command buttons' Shift captions in sync across focus changes, when key events aren't delivered here (e. g. while a dialog spawned by a command is open)
	void focusInEvent(QFocusEvent * event) override;
	void focusOutEvent(QFocusEvent * event) override;

	bool edit(const QModelIndex & index, EditTrigger trigger, QEvent * event) override;

	bool eventFilter(QObject* target, QEvent* event) override;

	void paintEvent(QPaintEvent* event) override;

private:
	void moveCursorToNextItem(bool invertSelection = false);
	void moveCursorToPreviousItem(bool invertSelection = false);
	void pgUp(bool invertSelection = false);
	void pgDn(bool invertSelection = false);

	[[nodiscard]] int numRowsVisible() const;

	// Sets the section sizes from _columnLayout's shares, summing to width
	void fitColumnsToWidth(int width);
	// Records a resize or move the user made in the header
	void storeColumnLayoutFromHeader();

private:
	std::vector<FileListViewEventObserver*> _eventObservers;

	QPersistentModelIndex               _currentItemBeforeMouseClick;
	std::optional<std::chrono::steady_clock::time_point> _timeToNextPaintStart;
	ColumnLayout                        _columnLayout; // The section sizes are fitted to it; only the user's header edits update it from them

	bool                                _applyingColumnLayout = false; // Keeps storeColumnLayoutFromHeader() from recording this view's own changes
	QPoint                              _singleMouseClickPos;
	bool                                _singleMouseClickValid = false;
	bool                                _shiftPressedItemSelected = false;
	bool                                _currentItemShouldBeSelectedOnMouseClick = false;
};
