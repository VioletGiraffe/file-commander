#pragma once

#include "favoritelocationslist/cfavoritelocations.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

class QTreeWidgetItem;

namespace Ui {
class CFavoriteLocationsEditor;
}

class CFavoriteLocationsListItem;

class CFavoriteLocationsEditor final : public QDialog
{
public:
	explicit CFavoriteLocationsEditor(QWidget *parent = nullptr) noexcept;
	~CFavoriteLocationsEditor() override;

	// Commits the edited fields first; a refusal keeps the dialog open
	void done(int result) override;

private:
	void currentItemChanged(QTreeWidgetItem * current, QTreeWidgetItem * previous);
	void contextMenu(const QPoint& pos);
	// Commits the name and location fields to the current item. False when refused: the fields show the stored values again.
	bool applyFieldEdits();

	// Rebuilds the tree, with the item of `current` as the current item
	void fillUI(const CLocationsCollection* current);
	void addLocationsToTreeWidget(std::vector<CLocationsCollection>& parentList, std::vector<CLocationsCollection>::iterator location, QTreeWidgetItem * parent);

private:
	Ui::CFavoriteLocationsEditor* const ui;
	CFavoriteLocations& _locations;
	CFavoriteLocationsListItem* _currentItem;
};
