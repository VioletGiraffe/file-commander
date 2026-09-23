#pragma once

#include "cpanel.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QPersistentModelIndex>
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

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
class CController;
class CFileListView final : public QTreeView
{
	Q_OBJECT

public:
	explicit CFileListView(QWidget *parent = nullptr);
	void addEventObserver(FileListViewEventObserver* observer);

	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(enum Panel p);

	void setHeaderAdjustmentRequired(bool required);

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

signals:
	void contextMenuRequested(QPoint pos);
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

private:
	void selectRegion(const QModelIndex& start, const QModelIndex& end);
	void moveCursorToNextItem(bool invertSelection = false);
	void moveCursorToPreviousItem(bool invertSelection = false);
	void pgUp(bool invertSelection = false);
	void pgDn(bool invertSelection = false);

	[[nodiscard]] int numRowsVisible() const;

private:
	std::vector<FileListViewEventObserver*> _eventObservers;

	QPersistentModelIndex               _currentItemBeforeMouseClick;

	enum Panel                          _panelPosition = Panel::UnknownPanel;
	bool                                _bHeaderAdjustmentRequired = true;
	QPoint                              _singleMouseClickPos;
	bool                                _singleMouseClickValid = false;
	bool                                _shiftPressedItemSelected = false;
	bool                                _currentItemShouldBeSelectedOnMouseClick = false;
};
