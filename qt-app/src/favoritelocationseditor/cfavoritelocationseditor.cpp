#include "cfavoritelocationseditor.h"

#include "ccontroller.h"
#include "diskenumerator/volumeinfo.hpp"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cfavoritelocationseditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <utility>
#include <vector>

using Kind = CLocationsCollection::Kind;

namespace {

enum Column { NameColumn = 0, PathColumn = 1 };
constexpr int KindRole = Qt::UserRole; // In NameColumn
constexpr int ResolvedPathRole = Qt::UserRole + 1; // In PathColumn: resolveUserPath() of the path text

Kind kindOf(const QModelIndex& index)
{
	return static_cast<Kind>(index.siblingAtColumn(NameColumn).data(KindRole).toInt());
}

Kind kindOf(const QTreeWidgetItem* item)
{
	return static_cast<Kind>(item->data(NameColumn, KindRole).toInt());
}

// Draws a separator as a line, and a browse button in the current location's path.
// Editing keeps a group's name and a location's path when they are cleared, and names a location without a name after its folder.
class CFavoritesItemDelegate final : public QStyledItemDelegate
{
public:
	CFavoritesItemDelegate(QObject* parent, std::function<void(const QModelIndex& pathIndex)> browseForFolder) :
		QStyledItemDelegate(parent),
		_browseForFolder(std::move(browseForFolder))
	{}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		const QStyle* const style = option.widget ? option.widget->style() : QApplication::style();
		if (kindOf(index) == Kind::Separator)
		{
			QStyleOptionViewItem itemOption = option;
			initStyleOption(&itemOption, index);
			style->drawPrimitive(QStyle::PE_PanelItemViewItem, &itemOption, painter, option.widget);

			painter->save();
			painter->setPen(option.palette.color(QPalette::Mid));
			const int y = option.rect.center().y();
			painter->drawLine(option.rect.left(), y, option.rect.right(), y);
			painter->restore();
			return;
		}

		if (!hasBrowseButton(option, index))
		{
			QStyledItemDelegate::paint(painter, option, index);
			return;
		}

		const QRect buttonRect = browseButtonRect(option.rect);
		QStyleOptionViewItem textOption = option;
		textOption.rect.setRight(buttonRect.left() - 1);
		QStyledItemDelegate::paint(painter, textOption, index);

		QStyleOptionButton button;
		button.initFrom(option.widget);
		button.rect = buttonRect;
		button.text = QStringLiteral("...");
		button.state = QStyle::State_Enabled | QStyle::State_Raised;
		style->drawControl(QStyle::CE_PushButton, &button, painter, option.widget);
	}

	bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override
	{
		const QEvent::Type type = event->type();
		if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick)
			return QStyledItemDelegate::editorEvent(event, model, option, index);

		const bool onButton = hasBrowseButton(option, index) && browseButtonRect(option.rect).contains(static_cast<QMouseEvent*>(event)->position().toPoint());
		if (type == QEvent::MouseButtonRelease)
		{
			// A press can make the row current, and with it draw the button under the cursor: only a press on a drawn button counts
			const bool clicked = onButton && _pressedBrowseButton == index;
			_pressedBrowseButton = {};
			if (clicked)
			{
				_browseForFolder(index);
				return true;
			}
		}
		else if (onButton)
		{
			if (type == QEvent::MouseButtonPress)
				_pressedBrowseButton = index;
			return true;
		}
		return QStyledItemDelegate::editorEvent(event, model, option, index);
	}

	void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyledItemDelegate::updateEditorGeometry(editor, option, index);
		// Leaves the browse button clickable while the path is edited
		if (hasBrowseButton(option, index))
			editor->setGeometry(editor->geometry().adjusted(0, 0, -browseButtonRect(option.rect).width(), 0));
	}

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& /*option*/, const QModelIndex& index) const override
	{
		const Kind kind = kindOf(index);
		if (kind == Kind::Separator || (kind == Kind::Group && index.column() == PathColumn))
			return nullptr;

		auto* const editor = new QLineEdit(parent);
		editor->setFrame(false);
		return editor;
	}

	void setEditorData(QWidget* editor, const QModelIndex& index) const override
	{
		static_cast<QLineEdit*>(editor)->setText(index.data(Qt::EditRole).toString());
	}

	void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
	{
		QString text = static_cast<QLineEdit*>(editor)->text();
		if (text.isEmpty())
		{
			if (index.column() == PathColumn || kindOf(index) == Kind::Group)
				return;
			text = defaultFavoriteName(index.siblingAtColumn(PathColumn).data().toString());
		}
		model->setData(index, text, Qt::EditRole);
	}

private:
	[[nodiscard]] static bool hasBrowseButton(const QStyleOptionViewItem& option, const QModelIndex& index)
	{
		if (index.column() != PathColumn || kindOf(index) != Kind::Location)
			return false;

		const auto* const view = qobject_cast<const QAbstractItemView*>(option.widget);
		return view && view->currentIndex().siblingAtColumn(PathColumn) == index;
	}

	[[nodiscard]] static QRect browseButtonRect(const QRect& cellRect)
	{
		const int width = cellRect.height() + 8;
		return { cellRect.right() - width + 1, cellRect.top(), width, cellRect.height() };
	}

private:
	const std::function<void(const QModelIndex& pathIndex)> _browseForFolder;
	QPersistentModelIndex _pressedBrowseButton; // The cell whose drawn button the mouse was pressed on
};

CLocationsCollection collectionOf(const QTreeWidgetItem* item)
{
	switch (kindOf(item))
	{
	case Kind::Location:
		return CLocationsCollection::location(item->text(NameColumn), item->text(PathColumn));
	case Kind::Separator:
		return CLocationsCollection::separator();
	case Kind::Group:
		break;
	}

	CLocationsCollection group = CLocationsCollection::group(item->text(NameColumn));
	for (int i = 0; i < item->childCount(); ++i)
		group.subLocations.push_back(collectionOf(item->child(i)));
	return group;
}

std::vector<QString> volumeRoots()
{
	std::vector<QString> roots;
	for (const VolumeInfo& volume : CController::get().volumes())
		roots.push_back(volume.rootObjectInfo.fullAbsolutePath());
	return roots;
}

} // namespace

CFavoriteLocationsEditor::CFavoriteLocationsEditor(QWidget* parent, QString currentFolderPosix) :
	QDialog(parent),
	ui(new Ui::CFavoriteLocationsEditor),
	_favorites(CController::get().favoriteLocations()),
	_currentFolderPosix(std::move(currentFolderPosix)),
	_availabilityChecker(volumeRoots())
{
	ui->setupUi(this);

	QPalette hintPalette = ui->_hint->palette();
	hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::PlaceholderText));
	ui->_hint->setPalette(hintPalette);

	_availabilityPollTimer.setInterval(100);
	connect(&_availabilityPollTimer, &QTimer::timeout, this, &CFavoriteLocationsEditor::showFinishedAvailabilityChecks);

	QTreeWidget* const tree = ui->_tree;
	tree->setItemDelegate(new CFavoritesItemDelegate(tree, [this](const QModelIndex& pathIndex) { browseForFolder(ui->_tree->itemFromIndex(pathIndex)); }));
	tree->header()->resizeSection(NameColumn, width() / 3);

	// Filling, adding and moving all insert items, so every group starts expanded
	connect(tree->model(), &QAbstractItemModel::rowsInserted, this, [tree](const QModelIndex& parentIndex, const int first, const int last) {
		for (int row = first; row <= last; ++row)
			tree->expandRecursively(tree->model()->index(row, NameColumn, parentIndex));
	});
	connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, const int column) {
		if (column == PathColumn && kindOf(item) == Kind::Location)
			updateAvailability(item);
	});

	for (const CLocationsCollection& location : _favorites.locations())
		tree->addTopLevelItem(newTreeItem(location));

	connect(ui->_btnAddLocation, &QPushButton::clicked, this, [this] {
		const QString path = toNativeSeparators(_currentFolderPosix);
		insertNewItem(CLocationsCollection::location(defaultFavoriteName(path), path));
	});
	connect(ui->_btnAddGroup, &QPushButton::clicked, this, [this] { insertNewItem(CLocationsCollection::group(tr("New group"))); });
	connect(ui->_btnAddSeparator, &QPushButton::clicked, this, [this] { insertNewItem(CLocationsCollection::separator()); });
	connect(ui->_btnDelete, &QPushButton::clicked, this, &CFavoriteLocationsEditor::deleteSelectedItems);
	connect(ui->_filter, &QLineEdit::textChanged, this, &CFavoriteLocationsEditor::applyFilter);

	new QShortcut(QKeySequence::Delete, tree, [this] { deleteSelectedItems(); }, Qt::WidgetShortcut);
	for (const Qt::Key key : { Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right })
		new QShortcut(QKeySequence{ Qt::ALT | key }, tree, [this, key] { moveCurrentItem(key); }, Qt::WidgetShortcut);
}

CFavoriteLocationsEditor::~CFavoriteLocationsEditor()
{
	delete ui;
}

void CFavoriteLocationsEditor::accept()
{
	std::vector<CLocationsCollection> locations;
	for (int i = 0; i < ui->_tree->topLevelItemCount(); ++i)
		locations.push_back(collectionOf(ui->_tree->topLevelItem(i)));
	_favorites.setLocations(std::move(locations));

	QDialog::accept();
}

QTreeWidgetItem* CFavoriteLocationsEditor::newTreeItem(const CLocationsCollection& favorite)
{
	auto* const item = new QTreeWidgetItem;
	item->setData(NameColumn, KindRole, static_cast<int>(favorite.kind));

	Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
	if (!favorite.isSeparator())
		flags |= Qt::ItemIsEditable;
	if (favorite.isGroup())
		flags |= Qt::ItemIsDropEnabled; // Items dropped onto a location or a separator go next to it
	item->setFlags(flags);

	item->setText(NameColumn, favorite.displayName);
	item->setText(PathColumn, favorite.path);

	if (favorite.isGroup())
	{
		QFont boldFont = item->font(NameColumn);
		boldFont.setBold(true);
		item->setFont(NameColumn, boldFont);

		for (const CLocationsCollection& subItem : favorite.subLocations)
			item->addChild(newTreeItem(subItem));
	}
	else if (!favorite.isSeparator())
		updateAvailability(item);

	return item;
}

void CFavoriteLocationsEditor::insertNewItem(const CLocationsCollection& favorite)
{
	ui->_filter->clear(); // So that the new item shows

	QTreeWidget* const tree = ui->_tree;
	QTreeWidgetItem* const current = tree->currentItem();
	QTreeWidgetItem* const item = newTreeItem(favorite);
	if (!current)
		tree->addTopLevelItem(item);
	else if (QTreeWidgetItem* const parent = current->parent())
		parent->insertChild(parent->indexOfChild(current) + 1, item);
	else
		tree->insertTopLevelItem(tree->indexOfTopLevelItem(current) + 1, item);

	tree->setCurrentItem(item);
	if (!favorite.isSeparator())
		tree->editItem(item, NameColumn);
}

void CFavoriteLocationsEditor::deleteSelectedItems()
{
	std::vector<QTreeWidgetItem*> itemsToDelete;
	for (QTreeWidgetItem* const item : ui->_tree->selectedItems())
	{
		// An item stays selected when the filter hides it
		if (item->isHidden())
			continue;

		// An item in a selected group goes with the group
		bool inSelectedGroup = false;
		for (const QTreeWidgetItem* parent = item->parent(); parent && !inSelectedGroup; parent = parent->parent())
			inSelectedGroup = parent->isSelected();
		if (!inSelectedGroup)
			itemsToDelete.push_back(item);
	}

	for (QTreeWidgetItem* const item : itemsToDelete)
		delete item;
}

void CFavoriteLocationsEditor::browseForFolder(QTreeWidgetItem* const item)
{
	assert_and_return_r(item, );

	// Opening a folder that was not found could block the dialog: it may be on an unreachable network share
	const QString resolvedPath = item->data(PathColumn, ResolvedPathRole).toString();
	const QString startFolder = _folderAvailability.value(resolvedPath, false) ? resolvedPath : _currentFolderPosix;
	const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the folder"), startFolder);
	if (!folder.isEmpty())
		item->setText(PathColumn, toNativeSeparators(folder));
}

void CFavoriteLocationsEditor::moveCurrentItem(const Qt::Key key)
{
	// A move among hidden items would land where the user cannot see, so moves need the whole tree, as drags do
	if (!ui->_filter->text().isEmpty())
		return;

	QTreeWidgetItem* const item = ui->_tree->currentItem();
	if (!item)
		return;

	QTreeWidgetItem* const root = ui->_tree->invisibleRootItem();
	QTreeWidgetItem* const parent = item->parent() ? item->parent() : root;
	const int index = parent->indexOfChild(item);

	QTreeWidgetItem* newParent = parent;
	int newIndex = index;
	switch (key)
	{
	case Qt::Key_Up:
		newIndex = index - 1;
		break;
	case Qt::Key_Down:
		newIndex = index + 1;
		break;
	case Qt::Key_Left:
		if (parent == root)
			return;
		newParent = parent->parent() ? parent->parent() : root;
		newIndex = newParent->indexOfChild(parent) + 1;
		break;
	case Qt::Key_Right:
		if (index == 0 || kindOf(parent->child(index - 1)) != Kind::Group)
			return;
		newParent = parent->child(index - 1);
		newIndex = newParent->childCount();
		break;
	default:
		assert_and_return_r(false, );
	}

	if (newParent == parent && (newIndex < 0 || newIndex >= parent->childCount()))
		return;

	parent->takeChild(index);
	newParent->insertChild(newIndex, item);
	ui->_tree->setCurrentItem(item);
}

void CFavoriteLocationsEditor::applyFilter(const QString& text)
{
	// groupMatched: an item in a matching group shows, separators included
	const std::function<bool(QTreeWidgetItem*, bool)> filterItem = [&](QTreeWidgetItem* const item, const bool groupMatched) {
		const bool matches = groupMatched || (kindOf(item) != Kind::Separator
			&& (item->text(NameColumn).contains(text, Qt::CaseInsensitive) || item->text(PathColumn).contains(text, Qt::CaseInsensitive)));

		bool childShown = false;
		for (int i = 0; i < item->childCount(); ++i)
		{
			if (filterItem(item->child(i), matches))
				childShown = true;
		}

		const bool shown = text.isEmpty() || matches || childShown;
		item->setHidden(!shown);
		if (childShown && !text.isEmpty())
			item->setExpanded(true);
		return shown;
	};

	for (int i = 0; i < ui->_tree->topLevelItemCount(); ++i)
		filterItem(ui->_tree->topLevelItem(i), false);

	// F2 edits the current item, so it must be one that shows
	if (const QTreeWidgetItem* const current = ui->_tree->currentItem(); current && current->isHidden())
	{
		const QTreeWidgetItemIterator firstShown{ ui->_tree, QTreeWidgetItemIterator::NotHidden };
		ui->_tree->selectionModel()->setCurrentIndex(*firstShown ? ui->_tree->indexFromItem(*firstShown) : QModelIndex{}, QItemSelectionModel::NoUpdate);
	}

	// Dropping among hidden items is as ambiguous as moving among them: see moveCurrentItem()
	ui->_tree->setDragDropMode(text.isEmpty() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void CFavoriteLocationsEditor::updateAvailability(QTreeWidgetItem* const item)
{
	const QString resolvedPath = resolveUserPath(item->text(PathColumn), _currentFolderPosix);
	// itemChanged also reports the roles set below and in showAvailability()
	if (const QVariant shownPath = item->data(PathColumn, ResolvedPathRole); shownPath.isValid() && shownPath.toString() == resolvedPath)
		return;

	item->setData(PathColumn, ResolvedPathRole, resolvedPath);
	if (resolvedPath.isEmpty())
	{
		showAvailability(item, false);
		return;
	}

	if (const auto known = _folderAvailability.constFind(resolvedPath); known != _folderAvailability.cend())
	{
		showAvailability(item, *known);
		return;
	}

	showAvailability(item, std::nullopt);
	_availabilityChecker.check(resolvedPath);
	_availabilityPollTimer.start();
}

void CFavoriteLocationsEditor::showAvailability(QTreeWidgetItem* const item, const std::optional<bool> available)
{
	QString toolTip;
	if (!available)
		toolTip = tr("Checking whether the folder exists...");
	else if (!*available)
		toolTip = tr("The folder was not found");

	const QVariant foreground = available == true ? QVariant{} : QVariant{ palette().brush(QPalette::Disabled, QPalette::Text) };
	for (const int column : { NameColumn, PathColumn })
	{
		item->setData(column, Qt::ForegroundRole, foreground);
		item->setToolTip(column, toolTip);
	}
}

void CFavoriteLocationsEditor::showFinishedAvailabilityChecks()
{
	for (const CFolderAvailabilityChecker::Result& result : _availabilityChecker.takeFinished())
	{
		_folderAvailability.insert(result.path, result.available);
		for (QTreeWidgetItemIterator it{ ui->_tree }; *it; ++it)
		{
			if (kindOf(*it) == Kind::Location && (*it)->data(PathColumn, ResolvedPathRole).toString() == result.path)
				showAvailability(*it, result.available);
		}
	}

	if (!_availabilityChecker.hasUnreportedChecks())
		_availabilityPollTimer.stop();
}
