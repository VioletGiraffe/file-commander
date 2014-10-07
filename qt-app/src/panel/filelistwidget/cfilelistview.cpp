#include "cfilelistview.h"
#include "../columns.h"
#include "model/cfilelistsortfilterproxymodel.h"
#include "delegate/cfilelistitemdelegate.h"

#include <assert.h>
#include <time.h>
#include <set>

#if defined __linux__ || defined __APPLE__
#include "cfocusframestyle.h"
#endif

CFileListView::CFileListView(QWidget *parent) :
	QTreeView(parent),
	_controller(CController::get()),
	_panelPosition(UnknownPanel),
	_bHeaderAdjustmentRequired(true),
	_bEditInProgress(false),
	_singleMouseClickValid(false),
	_shiftPressedItemSelected(false)
{
	setMouseTracking(true);
	setItemDelegate(new CFileListItemDelegate);
	connect(this, &QTreeView::doubleClicked, [this](const QModelIndex &idx) {

		_itemUnderCursorBeforeMouseClick = QModelIndex();
		_singleMouseClickValid = false;

		for(FileListViewEventObserver* observer: _eventObservers)
		{
			if (observer->fileListReturnPressOrDoubleClickPerformed(idx))
				break;
		}
	});

#if defined __linux__ || defined __APPLE__
	setStyle(new CFocusFrameStyle);
#endif
}

void CFileListView::addEventObserver(FileListViewEventObserver* observer)
{
	_eventObservers.push_back(observer);
}

// Sets the position (left or right) of a panel that this model represents
void CFileListView::setPanelPosition(enum Panel p)
{
	assert(_panelPosition == UnknownPanel); // Doesn't make sense to call this method more than once
	_panelPosition = p;
}

// Preserves item's selection state
void CFileListView::moveCursorToItem(const QModelIndex& index, bool invertSelection)
{
	if (index.isValid() && selectionModel()->model()->hasIndex(index.row(), index.column()))
	{
		const QModelIndex currentIdx = currentIndex();
		if (invertSelection && currentIdx.isValid())
		{
			for (int row = currentIdx.row(); row < index.row(); ++row)
				selectionModel()->setCurrentIndex(model()->index(row, 0), (!_shiftPressedItemSelected ? QItemSelectionModel::Select : QItemSelectionModel::Deselect) | QItemSelectionModel::Rows);
		}
		selectionModel()->setCurrentIndex(index, QItemSelectionModel::Current | QItemSelectionModel::Rows);
		scrollTo(index);
	}
}

// Header management
void CFileListView::saveHeaderState()
{
	if (header())
	{
		_headerGeometry = header()->saveGeometry();
		_headerState    = header()->saveState();
	}
}

void CFileListView::restoreHeaderState()
{
	if (header())
	{
		if (!_headerGeometry.isEmpty())
			header()->restoreGeometry(_headerGeometry);
		if (!_headerState.isEmpty())
			header()->restoreState(_headerState);
	}
}

// For managing selection and cursor
void CFileListView::mousePressEvent(QMouseEvent *e)
{
	_itemUnderCursorBeforeMouseClick = currentIndex();
	const QModelIndex itemClicked = indexAt(e->pos());

	// Always let Qt process this event
	QTreeView::mousePressEvent(e);

	// TODO: sometimes a double-clicked item remains selected. Find out why it happens.
	if (e->button() == Qt::LeftButton && itemClicked.isValid())
	{
		if (e->modifiers() == Qt::NoModifier)
		{
			selectionModel()->clearSelection();
		}
		else if (e->modifiers() == Qt::ControlModifier)
		{
			if (_itemUnderCursorBeforeMouseClick.isValid() && _itemUnderCursorBeforeMouseClick != itemClicked)
				selectionModel()->select(_itemUnderCursorBeforeMouseClick, QItemSelectionModel::Select | QItemSelectionModel::Rows);
		}
	}
}

void CFileListView::mouseMoveEvent(QMouseEvent * e)
{
	if (_singleMouseClickValid && (e->pos() - _singleMouseClickPos).manhattanLength() > 15)
	{
		_singleMouseClickValid = false;
		_itemUnderCursorBeforeMouseClick = QModelIndex();
		qDebug() << __FUNCTION__ << false;
	}
	QTreeView::mouseMoveEvent(e);
}

// For showing context menu
void CFileListView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event && (event->button() & Qt::RightButton))
	{
		// Selecting an item that was clicked upon
		const auto index = indexAt(event->pos());
		if (!index.isValid())
			clearSelection(); // clearing selection if there wasn't any item under cursor

		// Calling a context menu
		emit contextMenuRequested(QCursor::pos()); // Getting global screen coordinates
	}
	else if (event && event->button() == Qt::LeftButton)
	{
		_singleMouseClickValid = !_singleMouseClickValid;
		qDebug() << __FUNCTION__ << _singleMouseClickValid;
		const QModelIndex itemClicked = indexAt(event->pos());
		if (_itemUnderCursorBeforeMouseClick == itemClicked && _singleMouseClickValid)
		{
			_singleMouseClickPos = event->pos();
			QTimer * timer = new QTimer;
			timer->setSingleShot(true);
			QObject::connect(timer, &QTimer::timeout, [this, timer](){
				if (_singleMouseClickValid)
				{
					edit(model()->index(currentIndex().row(), 0), AllEditTriggers, nullptr);
					qDebug() << __FUNCTION__ << _singleMouseClickValid;
					_itemUnderCursorBeforeMouseClick = QModelIndex();
					_singleMouseClickValid = false;
				}

				timer->deleteLater();
			});
			timer->start(QApplication::doubleClickInterval()+50);
		}
	}

	// Always let Qt process this event
	QTreeView::mouseReleaseEvent(event);
}

// For managing selection and cursor
void CFileListView::keyPressEvent(QKeyEvent *event)
{
	if (_bEditInProgress)
	{
		QTreeView::keyPressEvent(event);
		return;
	}

	if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up ||
		event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp ||
		event->key() == Qt::Key_Home || event->key() == Qt::Key_End)
	{
		if ((event->modifiers() & (~Qt::KeypadModifier) & (~Qt::ShiftModifier)) == Qt::NoModifier)
		{
			const bool shiftPressed = (event->modifiers() & Qt::ShiftModifier) != 0;
			if (event->key() == Qt::Key_Down)
				moveCursorToNextItem(shiftPressed);
			else if (event->key() == Qt::Key_Up)
				moveCursorToPreviousItem(shiftPressed);
			else if (event->key() == Qt::Key_Home)
				moveCursorToItem(model()->index(0, 0), shiftPressed);
			else if (event->key() == Qt::Key_End)
				moveCursorToItem(model()->index(model()->rowCount()-1, 0), shiftPressed);
			else if (event->key() == Qt::Key_PageUp)
				pgUp(shiftPressed);
			else if (event->key() == Qt::Key_PageDown)
				pgDn(shiftPressed);

			event->accept();
			return;
		}
	}
	else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
	{
		if (event->modifiers() == Qt::NoModifier)
		{
			bool returnPressConsumed = false;
			for(FileListViewEventObserver* observer: _eventObservers)
			{
				returnPressConsumed = observer->fileListReturnPressed();
				if (returnPressConsumed)
					break;
			}

			if (!returnPressConsumed)
				for (FileListViewEventObserver* observer: _eventObservers)
				{
					if (currentIndex().isValid())
					{
						returnPressConsumed = observer->fileListReturnPressOrDoubleClickPerformed(currentIndex());
						if (returnPressConsumed)
							break;
					}
				}

		}
		else if (event->modifiers() == Qt::ControlModifier)
			emit ctrlEnterPressed();
		else if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
			emit ctrlShiftEnterPressed();

		return;
	}
	else if (event->key() == Qt::Key_Shift)
	{
		_shiftPressedItemSelected = currentIndex().isValid() ? selectionModel()->isSelected(currentIndex()) : false;
	}
	else
		emit keyPressed(event->text(), event->key(), event->modifiers());

	QTreeView::keyPressEvent(event);

#ifdef __linux__
	// FIXME: find out why this hack is necessary
	if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up ||
		event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp ||
		event->key() == Qt::Key_Home || event->key() == Qt::Key_End)
		if ((event->modifiers() & Qt::ShiftModifier) != 0)
			scrollTo(currentIndex());
#endif
}

bool CFileListView::edit(const QModelIndex & index, QAbstractItemView::EditTrigger trigger, QEvent * event)
{
	if (trigger & editTriggers())
		_bEditInProgress = true;
	return QTreeView::edit(model()->index(index.row(), 0), trigger, event);
}

// canDropMimeData is not called by QAbstractItemModel as of Qt 5.3 (https://bugreports.qt-project.org/browse/QTBUG-30534), so we're re-defining dragEnterEvent to fix this
void CFileListView::dragMoveEvent(QDragMoveEvent * event)
{
	QModelIndex targetIndex = indexAt(event->pos());
	if (model()->canDropMimeData(event->mimeData(), (Qt::DropAction)((int)event->possibleActions()), targetIndex.row(), targetIndex.column(), QModelIndex()))
	{
		if (state() != DraggingState)
			setState(DraggingState);
		QTreeView::dragMoveEvent(event);
	}
	else
		event->ignore();
}

void CFileListView::closeEditor(QWidget * editor, QAbstractItemDelegate::EndEditHint hint)
{
	_bEditInProgress = false;
	QTreeView::closeEditor(editor, hint);
}

void CFileListView::editorDestroyed(QObject * editor)
{
	_bEditInProgress = false;
	QTreeView::editorDestroyed(editor);
}

void CFileListView::selectRegion(const QModelIndex &start, const QModelIndex &end)
{
	bool itemBelongsToSelection = false;
	assert(selectionModel());
	for (int i = 0; i < model()->rowCount(); ++i)
	{
		// Start item found - beginning selection
		QModelIndex currentItem = model()->index(i, 0);
		if (!itemBelongsToSelection && (currentItem == start || currentItem == end))
		{
			itemBelongsToSelection = true;
			selectionModel()->select(currentItem, QItemSelectionModel::Select | QItemSelectionModel::Rows);
		}
		else if (itemBelongsToSelection && (currentItem == start || currentItem == end))
		{
			// End item found - finishing selection
			selectionModel()->select(currentItem, QItemSelectionModel::Select | QItemSelectionModel::Rows);
			return;
		}

		if (itemBelongsToSelection)
			selectionModel()->select(currentItem, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
}

void CFileListView::moveCursorToNextItem(bool invertSelection)
{
	if (model()->rowCount() <= 0)
		return;

	const QModelIndex curIdx(currentIndex());
	if (curIdx.isValid() && curIdx.row()+1 < model()->rowCount())
		moveCursorToItem(model()->index(curIdx.row()+1, 0), invertSelection);
	else if (!curIdx.isValid())
		moveCursorToItem(model()->index(0, 0), invertSelection);
}

void CFileListView::moveCursorToPreviousItem(bool invertSelection)
{
	if (model()->rowCount() <= 0)
		return;

	const QModelIndex curIdx(currentIndex());
	if (curIdx.isValid() && curIdx.row() > 0)
		moveCursorToItem(model()->index(curIdx.row()-1, 0), invertSelection);
	else if (!curIdx.isValid())
		moveCursorToItem(model()->index(0, 0), invertSelection);
}

void CFileListView::pgUp(bool invertSelection)
{
	const QModelIndex curIdx(currentIndex());
	if (!curIdx.isValid())
		return;

	const int numItemsVisible = numRowsVisible();
	if (numItemsVisible <= 0)
		return;

	moveCursorToItem(model()->index(std::max(curIdx.row()-numItemsVisible, 0), 0), invertSelection);
}

void CFileListView::pgDn(bool invertSelection)
{
	const QModelIndex curIdx(currentIndex());
	if (!curIdx.isValid())
		return;

	const int numItemsVisible = numRowsVisible();
	if (numItemsVisible <= 0)
		return;

	moveCursorToItem(model()->index(std::min(curIdx.row()+numItemsVisible, model()->rowCount()-1), 0), invertSelection);
}

int CFileListView::numRowsVisible() const
{
	// FIXME: rewrite it with indexAt to be O(1)
	int numRowsVisible = 0;
	for(int row = 0; row < model()->rowCount(); row++)
	{
		if (visualRect(model()->index(row, 0)).intersects(viewport()->rect()))
			++numRowsVisible;
	}
	return numRowsVisible;
}

void CFileListView::setHeaderAdjustmentRequired(bool required)
{
	_bHeaderAdjustmentRequired = required;
}

void CFileListView::modelAboutToBeReset()
{
	_itemUnderCursorBeforeMouseClick = QModelIndex();
	_singleMouseClickValid = false;
	if (_bHeaderAdjustmentRequired)
	{
		_bHeaderAdjustmentRequired = false;
		for (int i = 0; i < model()->columnCount(); ++i)
			resizeColumnToContents(i);
		sortByColumn(ExtColumn, Qt::AscendingOrder);
	}
}
