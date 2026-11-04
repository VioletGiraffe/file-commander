#pragma once

#include "plugin_export.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QList>
#include <QString>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <utility>

class QLabel;
class QMenu;

// A viewer plugin's widget. The host embeds it in a file panel or in a window; the widget never learns which.
// Focusable: its shortcuts fire while it or a child has focus. Never takes focus by itself: in a panel, the file list keeps it.
class PLUGIN_EXPORT CFileViewer : public QWidget
{
public:
	explicit CFileViewer(QWidget* parent = nullptr) noexcept;

	// Replaces the displayed file, keeping the user's view settings (zoom, delimiter, wrap...).
	// False on an unreadable or unsupported file: the error is then shown in place of the content.
	bool loadFile(const QString& path);
	// Releases any handle on the loaded file, keeping its content displayed: called before the host deletes, moves or renames it.
	virtual void closeFile() {}

	// A hosting window lists them in its File menu; the context menu lists them above the menus.
	[[nodiscard]] const QList<QAction*>& fileActions() const noexcept { return _fileActions; }
	// In menu bar order. A hosting window appends them to its menu bar; the context menu lists them as submenus.
	[[nodiscard]] const QList<QMenu*>& menus() const noexcept { return _menus; }
	// True: a hosting window sizes to sizeHint() instead of restoring its last geometry.
	[[nodiscard]] virtual bool sizesToContent() const { return false; }

protected:
	// Empty on success, otherwise the message for the user
	virtual QString tryLoadFile(const QString& path) = 0;

	// Takes the arguments of QWidget::addAction(). The shortcut is widget-scoped, as in addMenu().
	template <typename... Args>
	QAction* addFileAction(Args&&... args)
	{
		QAction* action = addAction(std::forward<Args>(args)...);
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		_fileActions.push_back(action);
		return action;
	}

	// Call once the menu is complete: every action in it gets a widget-scoped shortcut, so none competes with the host's.
	void addMenu(QMenu* menu);

	// loadFile() shows and hides the error itself; these serve a failure in any other operation
	void showLoadError(const QString& message);
	void hideLoadError();

	void contextMenuEvent(QContextMenuEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	QList<QAction*> _fileActions;
	QList<QMenu*> _menus;
	QLabel* _errorLabel = nullptr; // Created on the first error
};
