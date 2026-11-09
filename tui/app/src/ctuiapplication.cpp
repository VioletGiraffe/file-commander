#include "ctuiapplication.h"
#include "ctuipanel.h"

#include "ccontroller.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TSubMenu
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

CTuiApplication::CTuiApplication(CController& controller) :
	TProgInit(&CTuiApplication::initStatusLine, &CTuiApplication::initMenuBar, &TApplication::initDeskTop),
	_controller(controller)
{
	const TRect desktopArea = deskTop->getExtent();
	const short middle = (short)(desktopArea.b.x / 2);

	auto* leftPanel = new CTuiPanel({ 0, 0, middle, desktopArea.b.y }, _controller, Panel::LeftPanel);
	deskTop->insert(leftPanel);
	deskTop->insert(new CTuiPanel({ middle, 0, desktopArea.b.x, desktopArea.b.y }, _controller, Panel::RightPanel));
	leftPanel->select();
}

void CTuiApplication::getEvent(TEvent& event)
{
	QCoreApplication::processEvents();
	_controller.uiThreadTimerTick();
	TApplication::getEvent(event);
}

void CTuiApplication::handleEvent(TEvent& event)
{
	// Before the base class: the panel window would take Tab to move focus among its own views
	if (event.what == evKeyDown && event.keyDown.keyCode == kbTab)
	{
		deskTop->selectNext(False);
		clearEvent(event);
		return;
	}

	TApplication::handleEvent(event);
}

TMenuBar* CTuiApplication::initMenuBar(TRect r)
{
	r.b.y = r.a.y + 1;
	return new TMenuBar(r,
		*new TSubMenu("~F~ile", kbAltF) +
			*new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X")
	);
}

TStatusLine* CTuiApplication::initStatusLine(TRect r)
{
	r.a.y = r.b.y - 1;
	return new TStatusLine(r,
		*new TStatusDef(0, 0xFFFF) +
			*new TStatusItem("~Tab~ Switch panel", kbNoKey, 0) +
			*new TStatusItem("~Enter~ Open", kbNoKey, 0) +
			*new TStatusItem("~Backspace~ Up", kbNoKey, 0) +
			*new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
			*new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit)
	);
}
