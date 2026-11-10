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

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
// Turbo Vision enables win32-input-mode whenever it sets up the console.
// In that mode Windows Terminal sends each key as a ~20-byte sequence, which SSH can deliver in two parts.
// Turbo Vision waits only 10 ms for the rest of a sequence, then drops the key and inserts its bytes as text.
// Not covered: the console set up again after SIGCONT, which bypasses resume().
// Unix only: on Windows Turbo Vision reads console input records, not escape sequences.
void disableWin32InputMode()
{
#ifndef _WIN32
	static constexpr char disableSequence[] = "\x1B[?9001l";
	const int tty = ::open("/dev/tty", O_WRONLY | O_CLOEXEC);
	const int fd = tty >= 0 ? tty : STDOUT_FILENO; // The terminal Turbo Vision draws on
	[[maybe_unused]] const auto written = ::write(fd, disableSequence, sizeof(disableSequence) - 1);
	if (tty >= 0)
		::close(tty);
#endif
}
} // namespace

CTuiApplication::CTuiApplication(CController& controller) :
	TProgInit(&CTuiApplication::initStatusLine, &CTuiApplication::initMenuBar, &TApplication::initDeskTop),
	_controller(controller)
{
	disableWin32InputMode();

	const TRect desktopArea = deskTop->getExtent();
	const short middle = (short)(desktopArea.b.x / 2);

	_panels = {
		new CTuiPanel({ 0, 0, middle, desktopArea.b.y }, _controller, Panel::LeftPanel),
		new CTuiPanel({ middle, 0, desktopArea.b.x, desktopArea.b.y }, _controller, Panel::RightPanel)
	};
	for (CTuiPanel* panel : _panels)
		deskTop->insert(panel);
	_panels[(size_t)Panel::LeftPanel]->select();
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

	// Alt+F1 and Alt+F2 choose the volume of the left and the right panel, from either panel
	if (event.what == evKeyDown && (event.keyDown.keyCode == kbAltF1 || event.keyDown.keyCode == kbAltF2))
	{
		CTuiPanel* panel = _panels[(size_t)(event.keyDown.keyCode == kbAltF1 ? Panel::LeftPanel : Panel::RightPanel)];
		clearEvent(event);
		panel->select();
		panel->chooseVolume();
		return;
	}

	TApplication::handleEvent(event);
}

void CTuiApplication::resume()
{
	TApplication::resume();
	disableWin32InputMode();
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
			*new TStatusItem("~F2~ Rename", kbF2, cmRenameItem) +
			*new TStatusItem("~F5~ Copy", kbF5, cmCopyItems) +
			*new TStatusItem("~F6~ Move", kbF6, cmMoveItems) +
			*new TStatusItem("~F7~ Folder", kbF7, cmCreateFolder) +
			*new TStatusItem(nullptr, kbShiftF7, cmCreateFile) +
			*new TStatusItem("~F8~ Delete", kbF8, cmDeleteItems) +
			*new TStatusItem(nullptr, kbShiftF8, cmDeleteItemsPermanently) +
			*new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
			*new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit)
	);
}
