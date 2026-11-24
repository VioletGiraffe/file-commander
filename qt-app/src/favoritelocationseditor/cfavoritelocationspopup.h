#pragma once

#include "favoritelocationslist/cfavoritelocations.h"


DISABLE_COMPILER_WARNINGS
#include <QFrame>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>

class QLineEdit;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

// The favorites in a popup, under a filter box that keeps the focus. "Add current folder" comes first and "Manage..." last.
//   Typing lists the locations whose name or path holds the text, without their groups.
//   Enter or a click goes to the location: in the other panel with Shift. Up and Down move between rows, Esc and a click outside close.
//   The current folder's entries are in bold. Nothing here touches the file system.
//   Deletes itself once closed.
class CFavoriteLocationsPopup final : public QFrame
{
public:
	using GoTo = std::function<void(const QString& absolutePath, bool inOtherPanel)>;

	// currentFolderPosix: the folder "Add current folder" adds, and the one relative paths are resolved against.
	// manage: opens the favorites editor; called once the popup has closed. Requires a parent.
	CFavoriteLocationsPopup(CFavoriteLocations& favorites, QString currentFolderPosix, GoTo goTo, std::function<void()> manage, QWidget* parent);

	// At globalTopLeft, moved to stay on the screen
	void popUp(const QPoint& globalTopLeft);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void fillRows(const QString& filter);
	void activate(QTreeWidgetItem* item, bool inOtherPanel);

private:
	CFavoriteLocations& _favorites;
	const QString _currentFolderPosix;
	const GoTo _goTo;
	const std::function<void()> _manage;

	// Owned by this popup
	QLineEdit* _filter;
	QFrame* _listFrame; // The tree's frame and background
	QTreeWidget* _tree;
};
