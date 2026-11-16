#include "cfavoritelocationseditor.h"

#include "cnewfavoritelocationdialog.h"
#include "ccontroller.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "fileoperationsui/usermessagebox.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cfavoritelocationseditor.h"

#include <QMenu>
#include <QMessageBox>
#include <QTreeWidgetItemIterator>
RESTORE_COMPILER_WARNINGS

// Shows a category in bold.
// Refers to its data by an iterator, valid until a change to the favorites rebuilds the tree.
class CFavoriteLocationsListItem final : public QTreeWidgetItem
{
public:
	CFavoriteLocationsListItem(std::vector<CLocationsCollection>& parentList, const std::vector<CLocationsCollection>::iterator dataItemIterator) :
		QTreeWidgetItem(QStringList{ dataItemIterator->displayName }),
		_itemIterator(dataItemIterator),
		_parentList(parentList)
	{
		if (isCategory())
		{
			QFont boldFont = font(0);
			boldFont.setBold(true);
			setFont(0, boldFont);
		}
	}

	CFavoriteLocationsListItem& operator=(const CFavoriteLocationsListItem&) = delete;

	[[nodiscard]] std::vector<CLocationsCollection>::iterator itemIterator() const { return _itemIterator; }
	[[nodiscard]] std::vector<CLocationsCollection>& list() const { return _parentList; }
	[[nodiscard]] size_t index() const { return (size_t)(_itemIterator - _parentList.begin()); }
	[[nodiscard]] bool isCategory() const { return _itemIterator->isCategory(); }

private:
	const std::vector<CLocationsCollection>::iterator _itemIterator;
	std::vector<CLocationsCollection>& _parentList;
};

CFavoriteLocationsEditor::CFavoriteLocationsEditor(QWidget *parent) noexcept :
	QDialog(parent),
	ui(new Ui::CFavoriteLocationsEditor),
	_locations(CController::get().favoriteLocations()),
	_currentItem(nullptr)
{
	ui->setupUi(this);

	connect(ui->_list, &QTreeWidget::currentItemChanged, this, &CFavoriteLocationsEditor::currentItemChanged);
	connect(ui->_list, &QTreeWidget::customContextMenuRequested, this, &CFavoriteLocationsEditor::contextMenu);

	connect(ui->_nameEditor, &QLineEdit::editingFinished, this, &CFavoriteLocationsEditor::applyFieldEdits);
	connect(ui->_locationEditor, &QLineEdit::editingFinished, this, &CFavoriteLocationsEditor::applyFieldEdits);

	fillUI(nullptr);
}

CFavoriteLocationsEditor::~CFavoriteLocationsEditor()
{
	delete ui;
}

void CFavoriteLocationsEditor::done(const int result)
{
	if (applyFieldEdits())
		QDialog::done(result);
}

void CFavoriteLocationsEditor::currentItemChanged(QTreeWidgetItem* const current, QTreeWidgetItem* /*previous*/)
{
	applyFieldEdits(); // To the item that was current
	_currentItem = dynamic_cast<CFavoriteLocationsListItem*>(current);

	ui->_nameEditor->setEnabled(_currentItem != nullptr);
	ui->_locationEditor->setEnabled(_currentItem && !_currentItem->isCategory());
	ui->_nameEditor->setText(_currentItem ? _currentItem->itemIterator()->displayName : QString{});
	ui->_locationEditor->setText(_currentItem ? _currentItem->itemIterator()->absolutePath : QString{});
}

bool CFavoriteLocationsEditor::applyFieldEdits()
{
	if (!_currentItem)
		return true;

	const CLocationsCollection& stored = *_currentItem->itemIterator();
	const QString name = ui->_nameEditor->text();
	const QString location = _currentItem->isCategory() ? QString{} : ui->_locationEditor->text();
	if (name == stored.displayName && location == stored.absolutePath)
		return true;

	if (const auto refusal = _locations.editItem(_currentItem->list(), _currentItem->index(), name, location))
	{
		// Restored before the message: the focus the message takes ends the editing again
		ui->_nameEditor->setText(stored.displayName);
		ui->_locationEditor->setText(stored.absolutePath);
		showUserMessage(this, *refusal);
		return false;
	}

	_currentItem->setText(0, name);
	return true;
}

void CFavoriteLocationsEditor::contextMenu(const QPoint& pos)
{
	if (!applyFieldEdits())
		return;

	auto* const item = dynamic_cast<CFavoriteLocationsListItem*>(ui->_list->itemAt(pos));
	// A new item goes into the category clicked, next to the location clicked, or at the top level
	const auto listForNewItem = [this, item]() -> std::vector<CLocationsCollection>& {
		if (!item)
			return _locations.locations();
		return item->isCategory() ? item->itemIterator()->subLocations : item->list();
	};

	QMenu menu;
	QAction * addItemAction = nullptr;
	if (!item || item->isCategory())
		addItemAction = menu.addAction(tr("Add item..."));
	else
	{
		addItemAction = menu.addAction(tr("You can only add nested items to categories, not to end items"));
		addItemAction->setEnabled(false);
	}

	connect(addItemAction, &QAction::triggered, this, [this, listForNewItem](){
		CNewFavoriteLocationDialog dialog(this, false);
		if (dialog.exec() != QDialog::Accepted)
			return;

		std::vector<CLocationsCollection>& list = listForNewItem();
		if (const auto refusal = _locations.addLocation(list, dialog.name(), dialog.location()))
			showUserMessage(this, *refusal);
		else
			fillUI(&list.back());
	});

	connect(menu.addAction(tr("Add category...")), &QAction::triggered, this, [this, listForNewItem](){
		CNewFavoriteLocationDialog dialog(this, true);
		if (dialog.exec() != QDialog::Accepted)
			return;

		std::vector<CLocationsCollection>& list = listForNewItem();
		if (const auto refusal = _locations.addCategory(list, dialog.name()))
			showUserMessage(this, *refusal);
		else
			fillUI(&list.back());
	});

	if (item)
	{
		connect(menu.addAction(tr("Remove item")), &QAction::triggered, this, [this, item]() {
			if (item->isCategory() && QMessageBox::question(this, tr("Delete the item?"), tr("Are you sure you want to delete this category and all its sub-items?")) != QMessageBox::Yes)
				return;

			_locations.removeItem(item->list(), item->index());
			fillUI(nullptr);
		});
	}

	menu.exec(ui->_list->mapToGlobal(pos));
}

void CFavoriteLocationsEditor::fillUI(const CLocationsCollection* const current)
{
	_currentItem = nullptr;
	ui->_list->clear();
	for (auto it = _locations.locations().begin(); it != _locations.locations().end(); ++it)
		addLocationsToTreeWidget(_locations.locations(), it, nullptr);

	QTreeWidgetItem* currentItem = nullptr;
	for (QTreeWidgetItemIterator it{ ui->_list }; current && *it; ++it)
	{
		if (&*dynamic_cast<CFavoriteLocationsListItem*>(*it)->itemIterator() == current)
		{
			currentItem = *it;
			break;
		}
	}
	ui->_list->setCurrentItem(currentItem);
	currentItemChanged(currentItem, nullptr); // Not left to the signal: clear() may reset the current item without one
}

void CFavoriteLocationsEditor::addLocationsToTreeWidget(std::vector<CLocationsCollection>& parentList, const std::vector<CLocationsCollection>::iterator location, QTreeWidgetItem* const parent)
{
	assert_r(location->subLocations.empty() || location->isCategory());

	auto* const treeWidgetItem = new CFavoriteLocationsListItem(parentList, location);
	parent ? parent->addChild(treeWidgetItem) : ui->_list->addTopLevelItem(treeWidgetItem);
	for (auto it = location->subLocations.begin(); it != location->subLocations.end(); ++it)
		addLocationsToTreeWidget(location->subLocations, it, treeWidgetItem);

	if (location->isCategory())
		treeWidgetItem->setExpanded(true);
}
