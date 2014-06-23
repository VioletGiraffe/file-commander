#include "cfavoritelocationseditor.h"
#include "ui_cfavoritelocationseditor.h"

#include "cnewfavoritelocationdialog.h"
#include "ccontroller.h"

// If an item represents a subcategory, it cannot link to a location and can only be used as a container
class CFavoriteLocationsListItem : public QTreeWidgetItem
{
public:
	CFavoriteLocationsListItem(QTreeWidget * parent, std::list<CLocationsCollection>::iterator& dataItemIterator, bool isCategory) :
		QTreeWidgetItem(parent, QStringList() << dataItemIterator->displayName),
		_itemIterator(dataItemIterator),
		_bIsCategory(isCategory)
	{
		if (isCategory)
			setBold();
	}

	CFavoriteLocationsListItem(QTreeWidgetItem * parentItem, std::list<CLocationsCollection>::iterator& dataItemIterator, bool isCategory) :
		QTreeWidgetItem(parentItem, QStringList() << dataItemIterator->displayName),
		_itemIterator(dataItemIterator),
		_bIsCategory(isCategory)

	{
		if (isCategory)
			setBold();
	}

	std::list<CLocationsCollection>::iterator itemIterator() const {return _itemIterator;}
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
	bool _bIsCategory;
};

CFavoriteLocationsEditor::CFavoriteLocationsEditor(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CFavoriteLocationsEditor),
	_locations(CController::get().favoriteLocations())
{
	ui->setupUi(this);

	connect(ui->_list, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), SLOT(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)));
	connect(ui->_list, SIGNAL(customContextMenuRequested(const QPoint &)),this, SLOT(contextMenu(const QPoint &)));

	fillUI();
}

CFavoriteLocationsEditor::~CFavoriteLocationsEditor()
{
	delete ui;
}

void CFavoriteLocationsEditor::currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * previous)
{

}

void CFavoriteLocationsEditor::contextMenu(const QPoint & pos)
{
	CFavoriteLocationsListItem * item = dynamic_cast<CFavoriteLocationsListItem*>(ui->_list->itemAt(pos));
	QMenu menu;
	QAction * addItemAction = 0;
	if (!item || item->isCategory())
		addItemAction = menu.addAction("Add item...");
	else
	{
		addItemAction = menu.addAction("You can only add nested items to categories, not to end items");
		addItemAction->setEnabled(false);
	}

	connect(addItemAction, &QAction::triggered, [this, item](){
		CNewFavoriteLocationDialog dialog(this, false);
		if (dialog.exec() == QDialog::Accepted)
		{
			std::list<CLocationsCollection>& list = item ? item->itemIterator()->subLocations : _locations.locations();
			list.emplace_back(CLocationsCollection(dialog.name(), dialog.location()));
			if (item)
			{
				new CFavoriteLocationsListItem(item, --list.end(), false);
				item->setExpanded(true);
			}
			else
				new CFavoriteLocationsListItem(ui->_list, --list.end(), false);
		}
	});

	menu.exec(ui->_list->mapToGlobal(pos));
}

void CFavoriteLocationsEditor::fillUI()
{
	ui->_list->clear();
	for (auto it = _locations.locations().begin(); it != _locations.locations().end(); ++it)
		addLocationsToTreeWidget(it, 0);
}

void CFavoriteLocationsEditor::addLocationsToTreeWidget(std::list<CLocationsCollection>::iterator& locationCollectionListIterator, QTreeWidgetItem* parent)
{
	assert(locationCollectionListIterator->subLocations.empty() || locationCollectionListIterator->absolutePath.isEmpty());
	const bool isCategory = locationCollectionListIterator->absolutePath.isEmpty();
	CFavoriteLocationsListItem * treeWidgetItem = parent ? new CFavoriteLocationsListItem(parent, locationCollectionListIterator, isCategory) : new CFavoriteLocationsListItem(ui->_list, locationCollectionListIterator, isCategory);
	if (!locationCollectionListIterator->subLocations.empty())
		for (auto it = locationCollectionListIterator->subLocations.begin(); it != locationCollectionListIterator->subLocations.end(); ++it)
			addLocationsToTreeWidget(it, treeWidgetItem);
}
