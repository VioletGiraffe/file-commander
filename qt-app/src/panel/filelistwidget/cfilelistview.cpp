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
	_singleMouseClickValid(false)
{
	setMouseTracking(true);
	setItemDelegate(new CFileListItemDelegate);
	connect(this, &QTreeView::doubleClicked, [this](const QModelIndex &idx) {
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
void CFileListView::moveCursorToItem(const QModelIndex& index)
{
	if (index.isValid() && selectionModel()->model()->hasIndex(index.row(), index.column()))
	{
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
	const QModelIndex itemUnderCursor = currentIndex();
	const QModelIndex itemClicked = indexAt(e->pos());

	if (e->button() == Qt::LeftButton)
	{
		_singleMouseClickValid = !_singleMouseClickValid;
		if (itemUnderCursor == itemClicked && _singleMouseClickValid)
		{
			_singleMouseClickPos = e->pos();
			QTimer * timer = new QTimer;
			timer->setSingleShot(true);
			QObject::connect(timer, &QTimer::timeout, [this](){
				if (_singleMouseClickValid)
					edit(model()->index(currentIndex().row(), 0), AllEditTriggers, nullptr);
				_singleMouseClickValid = false;
			});
			timer->start(QApplication::doubleClickInterval()+50);
		}
	}

	QTreeView::mousePressEvent(e);

	if (e->button() == Qt::LeftButton && itemClicked.isValid())
	{
		if (e->modifiers() == Qt::NoModifier)
		{
			selectionModel()->clearSelection();
		}
		else if (e->modifiers() == Qt::ControlModifier)
		{
			if (itemUnderCursor.isValid() && itemUnderCursor != itemClicked)
				selectionModel()->select(itemUnderCursor, QItemSelectionModel::Select | QItemSelectionModel::Rows);
		}
	}
}

void CFileListView::mouseMoveEvent(QMouseEvent * e)
{
	if (_singleMouseClickValid && (e->pos() - _singleMouseClickPos).manhattanLength() > 15)
		_singleMouseClickValid = false;
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
		if ((event->modifiers() & (~Qt::KeypadModifier)) == Qt::NoModifier)
		{
			//TODO: PgUp, PgDwn, Home, End
			if (event->key() == Qt::Key_Down)
				moveCursorToNextItem();
			else if (event->key() == Qt::Key_Up)
				moveCursorToPreviousItem();
			else if (event->key() == Qt::Key_Home)
				moveCursorToItem(model()->index(0, 0));
			else if (event->key() == Qt::Key_End)
				moveCursorToItem(model()->index(model()->rowCount()-1, 0));
			else if (event->key() == Qt::Key_PageUp)
				pgUp();
			else if (event->key() == Qt::Key_PageDown)
				pgDn();

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
	else
		emit keyPressed(event->text(), event->key(), event->modifiers());

	QTreeView::keyPressEvent(event);
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

void CFileListView::moveCursorToNextItem()
{
	if (model()->rowCount() <= 0)
		return;

	const QModelIndex curIdx(currentIndex());
	if (curIdx.isValid() && curIdx.row()+1 < model()->rowCount())
		moveCursorToItem(model()->index(curIdx.row()+1, 0));
	else if (!curIdx.isValid())
		moveCursorToItem(model()->index(0, 0));
}

void CFileListView::moveCursorToPreviousItem()
{
	if (model()->rowCount() <= 0)
		return;

	const QModelIndex curIdx(currentIndex());
	if (curIdx.isValid() && curIdx.row() > 0)
		moveCursorToItem(model()->index(curIdx.row()-1, 0));
	else if (!curIdx.isValid())
		moveCursorToItem(model()->index(0, 0));
}

void CFileListView::pgUp()
{
	const QModelIndex curIdx(currentIndex());
	if (!curIdx.isValid())
		return;

	const int numItemsVisible = numRowsVisible();
	if (numItemsVisible <= 0)
		return;

	moveCursorToItem(model()->index(std::max(curIdx.row()-numItemsVisible, 0), 0));
}

void CFileListView::pgDn()
{
	const QModelIndex curIdx(currentIndex());
	if (!curIdx.isValid())
		return;

	const int numItemsVisible = numRowsVisible();
	if (numItemsVisible <= 0)
		return;

	moveCursorToItem(model()->index(std::min(curIdx.row()+numItemsVisible, model()->rowCount()-1), 0));
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
	_singleMouseClickValid = 0;
	if (_bHeaderAdjustmentRequired)
	{
		_bHeaderAdjustmentRequired = false;
		for (int i = 0; i < model()->columnCount(); ++i)
			resizeColumnToContents(i);
		sortByColumn(ExtColumn, Qt::AscendingOrder);
	}
}
