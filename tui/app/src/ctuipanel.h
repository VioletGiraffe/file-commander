#pragma once

#include "cpanel.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

class CController;
class CTuiFileList;

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
	void activateCurrentItem();
	void setTitle(const QString& text);
	[[nodiscard]] bool isActiveTab(Panel p, qulonglong tabId) const;

private:
	CController& _controller;
	const Panel _side;
	CTuiFileList* _fileList; // Owned by this window
};
