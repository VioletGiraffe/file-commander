#include "cfavoritelocationspopup.h"

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "paths/pathsyntax.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMargins>
#include <QMetaObject>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>
#include <vector>

namespace {

enum Column { NameColumn = 0, PathColumn = 1 };
constexpr int RowKindRole = Qt::UserRole; // In NameColumn
constexpr int ResolvedPathRole = Qt::UserRole + 1; // In NameColumn of a location: resolveUserPath() of its path

enum class RowKind { AddCurrentFolder, Manage, Location, Group, Separator };

#ifdef _WIN32
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseSensitive;
#endif

RowKind rowKind(const QTreeWidgetItem* item)
{
	return static_cast<RowKind>(item->data(NameColumn, RowKindRole).toInt());
}

QTreeWidgetItem* newRow(const RowKind kind, const QString& name, const QString& path = {})
{
	auto* const item = new QTreeWidgetItem{ QStringList{ name, path } };
	item->setData(NameColumn, RowKindRole, static_cast<int>(kind));
	return item;
}

void addRow(QTreeWidget& tree, QTreeWidgetItem* const parent, QTreeWidgetItem* const item)
{
	if (parent)
		parent->addChild(item);
	else
		tree.addTopLevelItem(item);
}

// A line across the row; the keyboard skips it
QTreeWidgetItem* addSeparator(QTreeWidget& tree, QTreeWidgetItem* const parent)
{
	QTreeWidgetItem* const item = newRow(RowKind::Separator, {});
	item->setFlags(Qt::NoItemFlags);
	addRow(tree, parent, item);
	item->setFirstColumnSpanned(true);

	auto* const line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	tree.setItemWidget(item, NameColumn, line);
	return item;
}

bool matchesFilter(const CLocationsCollection& location, const QString& filter)
{
	return location.displayName.contains(filter, Qt::CaseInsensitive) || location.path.contains(filter, Qt::CaseInsensitive);
}

} // namespace

CFavoriteLocationsPopup::CFavoriteLocationsPopup(CFavoriteLocations& favorites, QString currentFolderPosix, GoTo goTo, std::function<void()> manage, QWidget* parent) :
	QFrame{ parent },
	_favorites{ favorites },
	_currentFolderPosix{ std::move(currentFolderPosix) },
	_goTo{ std::move(goTo) },
	_manage{ std::move(manage) }
{
	assert_r(parent); // `manage` is queued on it, to run after this popup closes

	setWindowFlags(Qt::Popup);
	setAttribute(Qt::WA_DeleteOnClose);
	setFrameShape(QFrame::StyledPanel);

	_filter = new QLineEdit{ this };
	_filter->setPlaceholderText(tr("Filter by name or path"));
	_filter->setClearButtonEnabled(true);
	_filter->installEventFilter(this);

	// The list's frame: its margins leave space above the first row and below the last
	_listFrame = new QFrame{ this };
	_listFrame->setFrameShape(QFrame::StyledPanel);
	_listFrame->setAutoFillBackground(true);
	_listFrame->setBackgroundRole(QPalette::Base); // The rows' background, so the margins look part of the list

	_tree = new QTreeWidget{ _listFrame };
	_tree->setFrameShape(QFrame::NoFrame);
	_tree->setUniformRowHeights(true);
	_tree->setColumnCount(2);
	_tree->setHeaderHidden(true);
	_tree->setFocusPolicy(Qt::NoFocus);
	_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	_tree->setTextElideMode(Qt::ElideMiddle);
	_tree->setMouseTracking(true); // For itemEntered

	auto* const listLayout = new QVBoxLayout{ _listFrame };
	listLayout->addWidget(_tree);

	auto* const layout = new QVBoxLayout{ this };
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(_filter);
	layout->addWidget(_listFrame);

	connect(_filter, &QLineEdit::textChanged, this, &CFavoriteLocationsPopup::fillRows);
	// The row under the mouse is current, as in a menu
	connect(_tree, &QTreeWidget::itemEntered, this, [this](QTreeWidgetItem* item) {
		if (item->flags().testFlag(Qt::ItemIsEnabled))
			_tree->setCurrentItem(item);
	});
	// A group opens and closes with its arrow, so a click on it does nothing
	connect(_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
		if (rowKind(item) != RowKind::Group)
			activate(item, QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
	});

	fillRows({});
	const int halfRow = _tree->sizeHintForRow(0) / 2;
	listLayout->setContentsMargins(0, halfRow, 0, halfRow);
}

void CFavoriteLocationsPopup::popUp(const QPoint& globalTopLeft)
{
	const QScreen* const screenAtPosition = QGuiApplication::screenAt(globalTopLeft);
	const QRect available = (screenAtPosition ? screenAtPosition : screen())->availableGeometry();

	int rowCount = 0;
	for (QTreeWidgetItemIterator it{ _tree }; *it; ++it)
		++rowCount;

	const QMargins listMargins = _listFrame->layout()->contentsMargins();
	const int listHeight = 2 * _listFrame->frameWidth() + listMargins.top() + listMargins.bottom() + _tree->sizeHintForRow(0) * rowCount;
	const QMargins margins = layout()->contentsMargins();
	const int popupHeight = 2 * frameWidth() + margins.top() + margins.bottom() + _filter->sizeHint().height() + layout()->spacing() + listHeight;
	const int popupWidth = std::min(fontMetrics().averageCharWidth() * 70, available.width());

	QRect popupRect{ globalTopLeft, QSize{ popupWidth, std::min(popupHeight, available.height() * 2 / 3) } };
	if (popupRect.right() > available.right())
		popupRect.moveRight(available.right());
	if (popupRect.bottom() > available.bottom())
		popupRect.moveBottom(available.bottom());
	popupRect.moveTopLeft({ std::max(popupRect.left(), available.left()), std::max(popupRect.top(), available.top()) });
	setGeometry(popupRect);

	// Long names are elided rather than pushing the paths out of view
	_tree->resizeColumnToContents(NameColumn);
	_tree->setColumnWidth(NameColumn, std::min(_tree->columnWidth(NameColumn), popupWidth / 2));

	show();
	_filter->setFocus();
}

bool CFavoriteLocationsPopup::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != _filter || event->type() != QEvent::KeyPress)
		return QFrame::eventFilter(watched, event);

	auto* const keyEvent = static_cast<QKeyEvent*>(event);
	switch (keyEvent->key())
	{
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
		QCoreApplication::sendEvent(_tree, keyEvent);
		return true;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		activate(_tree->currentItem(), keyEvent->modifiers().testFlag(Qt::ShiftModifier));
		return true;
	case Qt::Key_Escape:
		close();
		return true;
	default:
		return QFrame::eventFilter(watched, event);
	}
}

void CFavoriteLocationsPopup::fillRows(const QString& filter)
{
	_tree->clear();

	const QString currentFolder = withTrailingSeparator(normalizedAbsolutePath(_currentFolderPosix));
	QTreeWidgetItem* firstLocation = nullptr;
	const auto addLocation = [&](const CLocationsCollection& location, QTreeWidgetItem* const parent) {
		QTreeWidgetItem* const item = newRow(RowKind::Location, location.displayName, location.path);
		item->setForeground(PathColumn, palette().brush(QPalette::PlaceholderText));
		addRow(*_tree, parent, item);

		const QString resolvedPath = resolveUserPath(location.path, _currentFolderPosix);
		item->setData(NameColumn, ResolvedPathRole, resolvedPath);
		if (resolvedPath.isEmpty())
		{
			item->setFlags(Qt::NoItemFlags);
			return;
		}

		if (withTrailingSeparator(resolvedPath).compare(currentFolder, pathCaseSensitivity) == 0)
		{
			QFont boldFont = item->font(NameColumn);
			boldFont.setBold(true);
			item->setFont(NameColumn, boldFont);
		}
		if (!firstLocation)
			firstLocation = item;
	};

	// While filtering: the matching locations only, out of their groups
	const std::function<void(const std::vector<CLocationsCollection>&, QTreeWidgetItem*)> addItems = [&](const std::vector<CLocationsCollection>& items, QTreeWidgetItem* const parent) {
		for (const CLocationsCollection& item : items)
		{
			if (item.isGroup())
			{
				if (!filter.isEmpty())
				{
					addItems(item.subLocations, nullptr);
					continue;
				}

				QTreeWidgetItem* const group = newRow(RowKind::Group, item.displayName);
				addRow(*_tree, parent, group);
				addItems(item.subLocations, group);
			}
			else if (item.isSeparator())
			{
				if (filter.isEmpty())
					addSeparator(*_tree, parent);
			}
			else if (filter.isEmpty() || matchesFilter(item, filter))
				addLocation(item, parent);
		}
	};

	QTreeWidgetItem* const addCurrentFolder = newRow(RowKind::AddCurrentFolder, tr("Add current folder"), toNativeSeparators(_currentFolderPosix));
	_tree->addTopLevelItem(addCurrentFolder);
	QTreeWidgetItem* const separatorAfterAdding = addSeparator(*_tree, nullptr);
	const int rowCountBeforeFavorites = _tree->topLevelItemCount();
	addItems(_favorites.locations(), nullptr);
	if (_tree->topLevelItemCount() == rowCountBeforeFavorites) // No favorite shows
		delete separatorAfterAdding;
	else
		addSeparator(*_tree, nullptr);
	_tree->addTopLevelItem(newRow(RowKind::Manage, tr("Manage...")));

	_tree->expandAll();
	// While filtering, Enter does nothing without a match
	_tree->setCurrentItem(firstLocation ? firstLocation : (filter.isEmpty() ? addCurrentFolder : nullptr));
}

void CFavoriteLocationsPopup::activate(QTreeWidgetItem* const item, const bool inOtherPanel)
{
	if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
		return;

	switch (rowKind(item))
	{
	case RowKind::AddCurrentFolder:
		assert_r(!_favorites.addLocation(_favorites.locations(), {}, toNativeSeparators(_currentFolderPosix)));
		close();
		break;
	case RowKind::Manage:
		close();
		// Once this popup's event handling has returned: the editor runs a modal event loop
		QMetaObject::invokeMethod(parentWidget(), [manage = _manage] { manage(); }, Qt::QueuedConnection);
		break;
	case RowKind::Group:
		item->setExpanded(!item->isExpanded());
		break;
	case RowKind::Location:
		close();
		_goTo(item->data(NameColumn, ResolvedPathRole).toString(), inOtherPanel);
		break;
	case RowKind::Separator:
		break;
	}
}
