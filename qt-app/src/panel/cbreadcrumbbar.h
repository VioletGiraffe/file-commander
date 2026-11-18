#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <vector>

class CHistoryComboBox;
class QMenu;
enum class PathBarStyle;

// The panel's path as a button per folder, painted in the applied theme's colours, over an editor for typing a path.
// Clicking a folder goes there; clicking the separator after it opens that folder's menu.
// Clicking the current folder or the space after it shows the editor, and so does the arrow, which also opens the editor's history.
// The editor is hidden again when focus moves to another widget in its window, except the menu bar; a popup or another window keeps it open.
// While editing, a strip widens to its longest history entry, within the row: the completion list is as wide as the bar.
// A path too wide for the bar loses its first folders to a "..." button, whose menu lists them.
class CBreadcrumbBar final : public QWidget
{
	Q_OBJECT

public:
	explicit CBreadcrumbBar(QWidget* parent = nullptr);

	// posixPath: absolute; ignored otherwise
	void setPath(const QString& posixPath);
	// The editor's list; currentRow: the entry of the current path
	void setHistory(const QStringList& items, int currentRow);
	// Must be called after the panel's activePanel property changes: the editor's stylesheet rules select on it
	void setShownAsActive(bool active);
	// Reads the applied theme: a strip is as wide as its path, the other styles as wide as the layout makes them
	void applyTheme();

	// Shows the editor, holding the path selected, and focuses it
	void startEditing();

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

signals:
	// The user picked an ancestor of the path; childOnPath is its child on the path. Both end with '/'.
	void goToAncestorRequested(const QString& ancestorPath, const QString& childOnPath);
	// Emitted before a folder's menu opens, to fill it; requires a direct connection. An empty menu is not shown.
	// folderPath ends with '/'.
	void folderMenuRequested(QMenu* menu, const QString& folderPath);
	// Typed into the editor, or picked from its history or completions
	void pathEntered(const QString& path);
	// Escape in the editor, which has gone back to its current entry
	void editingCancelled();

protected:
	bool eventFilter(QObject* object, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	struct Folder {
		QString path; // ends with '/'
		QString name; // native separators in a root's name
	};

	enum class PartKind { Folder, Separator, HiddenFolders };
	// A clickable area of the strip. folderIndex: the folder shown, the folder whose menu a separator opens,
	// or for HiddenFolders the number of folders hidden.
	struct Part {
		PartKind kind = PartKind::Folder;
		int folderIndex = 0;
		int left = 0, width = 0;
		QString text;
	};

	// Where a press or a hover lands: a part index, or one of these
	static constexpr int noPart = -1, arrowPart = -2, emptyArea = -3;

	void stopEditing();
	// Fits the parts into the current width
	void layOutParts();
	[[nodiscard]] int partAt(QPoint pos) const;
	void activatePart(int partIndex);
	void showMenuBelowPart(int partIndex, QMenu& menu);
	void setHoveredPart(int partIndex);

	[[nodiscard]] int barHeight() const;
	[[nodiscard]] QFont currentFolderFont() const;
	[[nodiscard]] QRect arrowRect() const;
	// The width of a folder's part, padding included, without eliding
	[[nodiscard]] int folderPartWidth(size_t folderIndex) const;
	// The strip's width with every folder shown
	[[nodiscard]] int fullStripWidth() const;

private:
	CHistoryComboBox* _editor = nullptr;
	std::vector<Folder> _folders;
	std::vector<Part> _parts;
	int _hoveredPart = noPart;
	int _pressedPart = noPart; // pressed with the mouse, or holding its menu open
	bool _shownAsActive = false;
	PathBarStyle _style{};
};
