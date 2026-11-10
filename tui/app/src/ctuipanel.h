#pragma once

#include "cpanel.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

class CController;
class CTuiFileList;
class CTuiFilterLine;
class CTuiStatusLine;

// The application's status line turns keys into these; the focused panel carries them out
inline constexpr ushort cmCopyItems = 1001, cmMoveItems = 1002, cmDeleteItems = 1003, cmDeleteItemsPermanently = 1004,
	cmRenameItem = 1005, cmCreateFolder = 1006, cmCreateFile = 1007, cmViewFile = 1008, cmEditFile = 1009;

// One side: a window showing the active tab's folder
class CTuiPanel final : public TWindow, public PanelContentsChangedListener
{
public:
	CTuiPanel(const TRect& bounds, CController& controller, Panel side);

	void handleEvent(TEvent& event) override;
	void setState(ushort aState, Boolean enable) override;

	void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) override;
	void onPanelContentsInvalidated(Panel p, qulonglong tabId) override;

	// Lists the volumes and shows the chosen one
	void chooseVolume();

private:
	void fillFromPanel(FileListRefreshCause cause = refreshCauseOther);
	// Copies the core's selection into the list
	void showCoreSelection();
	void updateStatusLine();

	void activateCurrentItem();
	void toggleCurrentItemSelection();
	// Asks for the pattern, prefilled with "*"
	void selectItemsByPattern(bool selected);
	void renameCurrentItem();
	// Each asks for the name, prefilled with the current item's
	void createFolder();
	void createFile();
	void goToTypedPath();
	// Empty for none and for [..]
	[[nodiscard]] QString currentItemName() const;
	// Empty unless the cursor is on a file
	[[nodiscard]] QString currentFilePath() const;
	// The same field again reverses the order
	void sortBy(FileListSortField field);

	void openFilterLine();
	void applyFilter(const QString& filterText);
	void closeFilterLine();
	// The filter line, while shown, takes the row above the status line
	void layOutViews();

	void setTitle(const QString& text);
	[[nodiscard]] bool isActiveTab(Panel p, qulonglong tabId) const;

private:
	CController& _controller;
	const Panel _side;
	// Owned by this window
	CTuiFileList* _fileList;
	CTuiFilterLine* _filterLine;
	CTuiStatusLine* _statusLine;
};
