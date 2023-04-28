#include "cfilelistview.h"
#include "../columns.h"
#include "delegate/cfilelistitemdelegate.h"

#include "assert/advanced_assert.h"
#include "math/math.hpp"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <time.h>
#include <set>

#if defined __linux__ || defined __APPLE__
#include "cfocusframestyle.h"
#endif

CFileListView::CFileListView(QWidget *parent) :
	QTreeView(parent)
{
	setMouseTracking(true);
	setItemDelegate(new CFileListItemDelegate{this});
	assert_r(connect(this, &QTreeView::doubleClicked, [this](const QModelIndex &idx) {
		_currentItemBeforeMouseClick = QModelIndex();
		_singleMouseClickValid = false;

		for(FileListViewEventObserver* observer: _eventObservers)
		{
			if (observer->fileListReturnPressOrDoubleClickPerformed(idx))
				break;
		}
	}));

	QHeaderView * headerView = header();
	assert_r(headerView);

	headerView->installEventFilter(this);

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
	assert_r(_panelPosition == UnknownPanel); // Doesn't make sense to call this method more than once
	_panelPosition = p;
}

// Preserves item's selection state
void CFileListView::moveCursorToItem(const QModelIndex& index, bool invertSelection)
{
	if (index.isValid() && selectionModel()->model()->hasIndex(index.row(), index.column()))
	{
		const QModelIndex normalizedTargetIndex = model()->index(index.row(), index.column()); // There was some problem with using the index directly, like it was from the wrong model or something
		const QModelIndex currentIdx = currentIndex();
		if (invertSelection && currentIdx.isValid())
		{
			for (int row = std::min(currentIdx.row(), normalizedTargetIndex.row()), endRow = std::max(currentIdx.row(), normalizedTargetIndex.row()); row <= endRow; ++row)
				selectionModel()->setCurrentIndex(model()->index(row, 0), (_shiftPressedItemSelected ? QItemSelectionModel::Deselect : QItemSelectionModel::Select) | QItemSelectionModel::Rows);
		}

		selectionModel()->setCurrentIndex(normalizedTargetIndex, QItemSelectionModel::Current | QItemSelectionModel::Rows);
		scrollTo(normalizedTargetIndex);
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
	QHeaderView * headerView = header();
	assert_and_return_r(headerView, );

	if (!_headerGeometry.isEmpty())
		headerView->restoreGeometry(_headerGeometry);
	if (!_headerState.isEmpty())
		headerView->restoreState(_headerState);
}

void CFileListView::invertSelection()
{
	QItemSelection allItems(model()->index(0, 0), model()->index(model()->rowCount() - 1, 0));
	selectionModel()->select(allItems, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
}

void CFileListView::modelAboutToBeReset()
{
	_currentItemBeforeMouseClick = QModelIndex();
	_singleMouseClickValid = false;
	if (_bHeaderAdjustmentRequired)
	{
		_bHeaderAdjustmentRequired = false;
		for (int i = 0; i < model()->columnCount(); ++i)
			resizeColumnToContents(i);

		sortByColumn(ExtColumn, Qt::AscendingOrder);
	}
}

bool CFileListView::editingInProgress() const
{
	return (state() & QAbstractItemView::EditingState) != 0;
}

// For managing selection and cursor
void CFileListView::mousePressEvent(QMouseEvent *e)
{
	_singleMouseClickValid = !_singleMouseClickValid && e->modifiers() == Qt::NoModifier;
	_currentItemBeforeMouseClick = currentIndex();
	const bool selectionWasEmpty = selectionModel()->selectedRows().empty();

	// Always let Qt process this event
	QTreeView::mousePressEvent(e);

	if (_currentItemShouldBeSelectedOnMouseClick && e->modifiers() == Qt::ControlModifier && selectionWasEmpty && _currentItemBeforeMouseClick.isValid())
	{
		_currentItemShouldBeSelectedOnMouseClick = false;
		selectionModel()->select(_currentItemBeforeMouseClick, QItemSelectionModel::Rows | QItemSelectionModel::Select);
	}
}

void CFileListView::mouseMoveEvent(QMouseEvent * e)
{
	if (_singleMouseClickValid && (e->pos() - _singleMouseClickPos).manhattanLength() > 15)
	{
		_singleMouseClickValid = false;
		_currentItemBeforeMouseClick = QModelIndex();
	}

	QTreeView::mouseMoveEvent(e);
}

// For showing context menu
void CFileListView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton)
	{
		// Selecting an item that was clicked upon
		const auto index = indexAt(event->pos());
		if (!index.isValid())
			clearSelection(); // clearing selection if there wasn't any item under cursor

		// Calling a context menu
		emit contextMenuRequested(QCursor::pos()); // Getting global screen coordinates
	}
	else if (event->button() == Qt::LeftButton)
	{
		const QModelIndex itemClicked = indexAt(event->pos());
		if (_currentItemBeforeMouseClick == itemClicked && _singleMouseClickValid && event->modifiers() == Qt::NoModifier)
		{
			_singleMouseClickPos = event->pos();
			QTimer::singleShot(QApplication::doubleClickInterval() + 50, this, [this]() {
				if (_singleMouseClickValid)
				{
					edit(model()->index(currentIndex().row(), 0), AllEditTriggers, nullptr);
					_currentItemBeforeMouseClick = QModelIndex();
					_singleMouseClickValid = false;
				}
			});
		}

		// Bug fix for #49 - preventing item selection with a single LMB click
		if (event->modifiers() == Qt::NoModifier && itemClicked.isValid())
			selectionModel()->clearSelection();
	}

	// Always let Qt process this event
	QTreeView::mouseReleaseEvent(event);
}

// For managing selection and cursor
void CFileListView::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Control)
		_currentItemShouldBeSelectedOnMouseClick = true;

	if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up ||
		event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp ||
		event->key() == Qt::Key_Home || event->key() == Qt::Key_End)
	{
		if ((event->modifiers() & ~Qt::KeypadModifier & ~Qt::ShiftModifier) == Qt::NoModifier)
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
		const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
		if (modifiers == Qt::NoModifier)
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
		else if (modifiers == Qt::ControlModifier)
			emit ctrlEnterPressed();
		else if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier))
			emit ctrlShiftEnterPressed();

		return;
	}
	else if (event->key() == Qt::Key_Shift)
	{
		_shiftPressedItemSelected = currentIndex().isValid() ? selectionModel()->isSelected(currentIndex()) : false;
	}
#ifdef __APPLE__ // TODO: Probably a Qt bug; remove this code when it's fixed
	else if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier)
	{
		edit(currentIndex(), QAbstractItemView::EditKeyPressed, event);
	}
#endif
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

void CFileListView::keyReleaseEvent(QKeyEvent * event)
{
	if (event->key() == Qt::Key_Control)
		_currentItemShouldBeSelectedOnMouseClick = true;
	else if (event->key() == Qt::Key_Shift)
		_shiftPressedItemSelected = false;

	QTreeView::keyReleaseEvent(event);
}

bool CFileListView::edit(const QModelIndex & index, QAbstractItemView::EditTrigger trigger, QEvent * event)
{
	return QTreeView::edit(model()->index(index.row(), 0), trigger, event);
}

bool CFileListView::eventFilter(QObject* target, QEvent* event)
{
	static CTimeElapsed g_timer{ true };
	static bool firstUpdate{ true };

	QHeaderView * headerView = header();
	if (target == headerView && event && event->type() == QEvent::Resize && headerView->count() == NumberOfColumns)
	{
		auto* resizeEvent = dynamic_cast<QResizeEvent*>(event);
		assert_and_return_r(resizeEvent, QTreeView::eventFilter(target, event));
		float oldHeaderWidth = 0.0f;
		for (int i = 0; i < headerView->count(); ++i)
			oldHeaderWidth += (float)headerView->sectionSize(i);

		const auto newHeaderWidth = (float)resizeEvent->size().width();
		if (oldHeaderWidth <= 0.0f || newHeaderWidth <= 0.0f || ::fabs(oldHeaderWidth - newHeaderWidth) < 0.1f)
			return QTreeView::eventFilter(target, event);

		std::array<float, NumberOfColumns> relativeColumnSizes;
		relativeColumnSizes.fill(0.0f);

		for (int i = 0; i <NumberOfColumns; ++i)
			relativeColumnSizes[i] = (float)headerView->sectionSize(i) / oldHeaderWidth;

		for (int i = 0; i < headerView->count(); ++i)
			headerView->resizeSection(i, Math::round<int>(newHeaderWidth * relativeColumnSizes[i]));
	}
	else if (event->type() == QEvent::Paint && model() && model()->rowCount() > 1000)
	{
		if (firstUpdate)
		{
			firstUpdate = false;
			qInfo() << "Time to first update:" << g_timer.elapsed();
		}
	}

	return QTreeView::eventFilter(target, event);
}

void CFileListView::selectRegion(const QModelIndex &start, const QModelIndex &end)
{
	bool itemBelongsToSelection = false;
	assert_r(selectionModel());
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
	const auto viewportRect = viewport()->rect();
	const auto topIndex = indexAt(QPoint{ 10, viewportRect.top() + 1 });
	const auto bottomIndex = indexAt(QPoint{ 10, viewportRect.bottom() - 1 });

	if (!topIndex.isValid())
	{
		assert_r(!bottomIndex.isValid());
		return 0;
	}

	return bottomIndex.isValid() ? bottomIndex.row() - topIndex.row() : model()->rowCount() - topIndex.row();
}

void CFileListView::setHeaderAdjustmentRequired(bool required)
{
	_bHeaderAdjustmentRequired = required;
}
