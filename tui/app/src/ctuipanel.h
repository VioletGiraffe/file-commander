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

// One side: a window showing the active tab's folder
class CTuiPanel final : public TWindow, public PanelContentsChangedListener
{
public:
	CTuiPanel(const TRect& bounds, CController& controller, Panel side);

	void handleEvent(TEvent& event) override;
	void setState(ushort aState, Boolean enable) override;

	void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) override;
	void onPanelContentsInvalidated(Panel p, qulonglong tabId) override;

private:
	void fillFromPanel();
	// Copies the core's selection into the list
	void showCoreSelection();
	void updateStatusLine();

	void activateCurrentItem();
	void toggleCurrentItemSelection();
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
