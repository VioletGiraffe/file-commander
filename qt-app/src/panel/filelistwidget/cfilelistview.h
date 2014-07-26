 #pragma once

#include "cpanel.h"
#include "ccontroller.h"
#include "../../QtAppIncludes"
#include <vector>

// Qt signals/slots system doesn't apply here because there should be a list of observers, and the signal shall not go further once it's been consumed by a listener

struct FileListViewEventObserver {
	virtual ~FileListViewEventObserver() {}

	virtual bool fileListReturnPressed() = 0;
	virtual bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& index) = 0;
};

struct FileListReturnPressedObserver : FileListViewEventObserver {
	bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex&) override {return false;}
};

struct FileListReturnPressOrDoubleClickObserver : FileListViewEventObserver {
	bool fileListReturnPressed() override {return false;}
};

class QMouseEvent;
class CController;
class CFileListView : public QTreeView
{
	Q_OBJECT

public:
	explicit CFileListView(QWidget *parent = 0);
	void addEventObserver(FileListViewEventObserver* observer);

	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(enum Panel p);

	void setHeaderAdjustmentRequired(bool required);

	// Preserves item's selection state
	void moveCursorToItem(const QModelIndex &item, bool invertSelection = false);

	// Header management
	void saveHeaderState();
	void restoreHeaderState();

signals:
	void contextMenuRequested (QPoint pos);
	void ctrlEnterPressed();
	void ctrlShiftEnterPressed();
	void keyPressed(QString keyText, int key, Qt::KeyboardModifiers modifiers);

protected:
	// For controlling selection
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	// For showing context menu
	void mouseReleaseEvent(QMouseEvent *event) override;
	// For managing selection and cursor
	void keyPressEvent(QKeyEvent * event) override;

	bool edit ( const QModelIndex & index, EditTrigger trigger, QEvent * event ) override;

	void dragMoveEvent(QDragMoveEvent * event) override;

protected slots:
	void closeEditor ( QWidget * editor, QAbstractItemDelegate::EndEditHint hint ) override;
	void editorDestroyed ( QObject * editor ) override;

private:
	void selectRegion(const QModelIndex& start, const QModelIndex& end);
	void moveCursorToNextItem(bool invertSelection = false);
	void moveCursorToPreviousItem(bool invertSelection = false);
	void pgUp(bool invertSelection = false);
	void pgDn(bool invertSelection = false);

	int numRowsVisible() const;

private slots:
	void modelAboutToBeReset();

private:
	std::vector<FileListViewEventObserver*> _eventObservers;
	QByteArray                          _headerGeometry;
	QByteArray                          _headerState;
	CController                       & _controller;
	enum Panel                          _panelPosition;
	bool                                _bHeaderAdjustmentRequired;
	bool                                _bEditInProgress;
	QPoint                              _singleMouseClickPos;
	bool                                _singleMouseClickValid;
	bool                                _shiftPressedItemSelected;
};
