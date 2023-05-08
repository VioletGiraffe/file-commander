#include "cfavoritelocationseditor.h"
#include "cnewfavoritelocationdialog.h"
#include "ccontroller.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfavoritelocationseditor.h"

#include <QMenu>
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

// If an item represents a subcategory, it cannot link to a location and can only be used as a container
class CFavoriteLocationsListItem final : public QTreeWidgetItem
{
public:
	CFavoriteLocationsListItem(QTreeWidget * parent, std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator& dataItemIterator, bool isCategory) :
		QTreeWidgetItem(parent, QStringList() << dataItemIterator->displayName),
		_itemIterator(dataItemIterator),
		_parentList(parentList),
		_bIsCategory(isCategory)
	{
		if (isCategory)
			setBold();
	}

	CFavoriteLocationsListItem(QTreeWidgetItem * parentItem, std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator& dataItemIterator, bool isCategory) :
		QTreeWidgetItem(parentItem, QStringList() << dataItemIterator->displayName),
		_itemIterator(dataItemIterator),
		_parentList(parentList),
		_bIsCategory(isCategory)

	{
		if (isCategory)
			setBold();
	}

	CFavoriteLocationsListItem& operator=(const CFavoriteLocationsListItem&) = delete;

	std::list<CLocationsCollection>::iterator itemIterator() const {return _itemIterator;}
	std::list<CLocationsCollection>& list() {return _parentList;}
	bool isCategory() const {return _bIsCategory;}

private:
	void setBold()
	{
		QFont newFont(font(0));
		newFont.setBold(true);
		setFont(0, newFont);
	}

private:
	std::list<CLocationsCollection>::iterator _itemIterator;
	std::list<CLocationsCollection>& _parentList;
	bool _bIsCategory;
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

	connect(ui->_locationEditor, &QLineEdit::textEdited, this, &CFavoriteLocationsEditor::locationEdited);
	connect(ui->_nameEditor, &QLineEdit::textEdited, this, &CFavoriteLocationsEditor::nameEdited);

	fillUI();
}

CFavoriteLocationsEditor::~CFavoriteLocationsEditor()
{
	delete ui;
}

void CFavoriteLocationsEditor::currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * /*previous*/)
{
	_currentItem = dynamic_cast<CFavoriteLocationsListItem*>(current);
	if (_currentItem)
	{
		ui->_locationEditor->setEnabled(true);
		ui->_nameEditor->setEnabled(true);

		ui->_locationEditor->setText(_currentItem->itemIterator()->absolutePath);
		ui->_nameEditor->setText(_currentItem->itemIterator()->displayName);
	}
	else
	{
		ui->_locationEditor->setEnabled(false);
		ui->_nameEditor->setEnabled(false);

		ui->_locationEditor->clear();
		ui->_nameEditor->clear();
	}
}

void CFavoriteLocationsEditor::contextMenu(const QPoint & pos)
{
	auto* item = dynamic_cast<CFavoriteLocationsListItem*>(ui->_list->itemAt(pos));
	QMenu menu;
	QAction * addItemAction = nullptr;
	if (!item || item->isCategory())
		addItemAction = menu.addAction(tr("Add item..."));
	else
	{
		addItemAction = menu.addAction(tr("You can only add nested items to categories, not to end items"));
		addItemAction->setEnabled(false);
	}

	connect(addItemAction, &QAction::triggered, this, [this, item](){
		CNewFavoriteLocationDialog dialog(this, false);
		if (dialog.exec() == QDialog::Accepted)
		{
			std::list<CLocationsCollection>& list = item ? item->itemIterator()->subLocations : _locations.locations();
			if (std::find_if(list.cbegin(), list.cend(), [&dialog](const CLocationsCollection& entry){return entry.absolutePath == dialog.location();}) != list.cend())
			{
				QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("This item already exists here (possibly under a different name)."), QMessageBox::Cancel);
				return;
			}
			else if (std::find_if(list.cbegin(), list.cend(), [&dialog](const CLocationsCollection& entry){return entry.displayName == dialog.name();}) != list.cend())
			{
				QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("And item with the same name already exists here (possibly pointing to a different location)."), QMessageBox::Cancel);
				return;
			}

			_locations.addItem(list, dialog.name(), dialog.location());

			if (item)
			{
				new CFavoriteLocationsListItem(item, list, --list.end(), false);
				item->setExpanded(true);
			}
			else
				new CFavoriteLocationsListItem(ui->_list, list, --list.end(), false);
		}
	});

	connect(menu.addAction(tr("Add category...")), &QAction::triggered, this, [this, item](){
		CNewFavoriteLocationDialog dialog(this, true);
		if (dialog.exec() == QDialog::Accepted)
		{
			std::list<CLocationsCollection>& list = item ? item->itemIterator()->subLocations : _locations.locations();
			if (std::find_if(list.cbegin(), list.cend(), [&dialog](const CLocationsCollection& entry){return entry.displayName == dialog.name();}) != list.cend())
			{
				QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("And item with the same name already exists here (possibly pointing to a different location)."), QMessageBox::Cancel);
				return;
			}

			_locations.addItem(list, dialog.name());
			if (item)
			{
				new CFavoriteLocationsListItem(item, list, --list.end(), true);
				item->setExpanded(true);
			}
			else
				new CFavoriteLocationsListItem(ui->_list, list, --list.end(), true);
		}
	});

	if (item)
	{
		connect(menu.addAction(tr("Remove item")), &QAction::triggered, this, [this, item]() {
			if (item->isCategory())
			{
				if (QMessageBox::question(this, tr("Delete the item?"), tr("Are you sure you want to delete this category and all its sub-items?")) == QMessageBox::Yes)
				{
					item->list().erase(item->itemIterator());
					delete item;
				}
			}
			else
			{
				item->list().erase(item->itemIterator());
				delete item;
			}

			_locations.save();
		});
	}

	menu.exec(ui->_list->mapToGlobal(pos));
}

void CFavoriteLocationsEditor::nameEdited(const QString& newName)
{
	if (_currentItem)
	{
		_currentItem->itemIterator()->displayName = newName;
		_currentItem->setData(0, Qt::DisplayRole, newName);
	}
}

void CFavoriteLocationsEditor::locationEdited(const QString& newLocation)
{
	if (_currentItem)
		_currentItem->itemIterator()->absolutePath = newLocation;
}

void CFavoriteLocationsEditor::fillUI()
{
	ui->_list->clear();
	for (auto it = _locations.locations().begin(); it != _locations.locations().end(); ++it)
		addLocationsToTreeWidget(_locations.locations(), it, nullptr);

	ui->_list->setCurrentItem(nullptr);
}

void CFavoriteLocationsEditor::addLocationsToTreeWidget(std::list<CLocationsCollection>& parentList, std::list<CLocationsCollection>::iterator& locationCollectionListIterator, QTreeWidgetItem* parent)
{
	assert_r(locationCollectionListIterator->subLocations.empty() || locationCollectionListIterator->absolutePath.isEmpty());

	const bool isCategory = locationCollectionListIterator->absolutePath.isEmpty();
	CFavoriteLocationsListItem * treeWidgetItem = parent ? new CFavoriteLocationsListItem(parent, parentList, locationCollectionListIterator, isCategory) : new CFavoriteLocationsListItem(ui->_list, parentList, locationCollectionListIterator, isCategory);
	if (!locationCollectionListIterator->subLocations.empty())
	{
		for (auto it = locationCollectionListIterator->subLocations.begin(); it != locationCollectionListIterator->subLocations.end(); ++it)
			addLocationsToTreeWidget(locationCollectionListIterator->subLocations, it, treeWidgetItem);
	}

	if (isCategory)
		treeWidgetItem->setExpanded(true);
}
