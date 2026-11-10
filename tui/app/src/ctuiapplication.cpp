#include "ctuiapplication.h"
#include "ctuipanel.h"

#include "ccontroller.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TClipboard
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
#include <QByteArray>
#include <QByteArrayView>
#include <QCoreApplication>
#include <QProcess>
RESTORE_COMPILER_WARNINGS

#include <cstdio>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
BOOL WINAPI ignoreInterrupt(DWORD controlType)
{
	return controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT;
}
#endif

#ifndef _WIN32
// Writes past Turbo Vision's output buffer, to the terminal it draws on
void writeToTerminal(QByteArrayView bytes)
{
	const int tty = ::open("/dev/tty", O_WRONLY | O_CLOEXEC);
	const int fd = tty >= 0 ? tty : STDOUT_FILENO;
	while (!bytes.isEmpty())
	{
		const ssize_t written = ::write(fd, bytes.data(), (size_t)bytes.size());
		if (written > 0)
			bytes = bytes.sliced(written);
		else if (written == 0 || errno != EINTR)
			break;
	}
	if (tty >= 0)
		::close(tty);
}
#endif

// Turbo Vision enables win32-input-mode whenever it sets up the console.
// In that mode Windows Terminal sends each key as a ~20-byte sequence, which SSH can deliver in two parts.
// Turbo Vision waits only 10 ms for the rest of a sequence, then drops the key and inserts its bytes as text.
// Not covered: the console set up again after SIGCONT, which bypasses resume().
// Unix only: on Windows Turbo Vision reads console input records, not escape sequences.
void disableWin32InputMode()
{
#ifndef _WIN32
	writeToTerminal("\x1B[?9001l");
#endif
}
} // namespace

CTuiApplication::CTuiApplication(CController& controller) :
	TProgInit(&CTuiApplication::initStatusLine, &CTuiApplication::initMenuBar, &CTuiApplication::initDeskTop),
	_controller(controller)
{
	disableWin32InputMode();
	insert(initHintLine(getExtent()));

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

std::expected<void, QString> CTuiApplication::runInTerminal(const QString& program, const QStringList& arguments, const QString& workingDir, bool waitForEnter)
{
	QProcess process;
	process.setProgram(program);
	process.setArguments(arguments);
	process.setWorkingDirectory(workingDir);
	process.setProcessChannelMode(QProcess::ForwardedChannels);
	process.setInputChannelMode(QProcess::ForwardedInputChannel);
#ifndef _WIN32
	process.setUnixProcessParameters(QProcess::UnixProcessFlag::ResetSignalHandlers); // The program must not inherit the ignored interrupts
#endif

	application->suspend();

	// The terminal sends Ctrl+C to every process attached to it: only the program must stop
#ifdef _WIN32
	SetConsoleCtrlHandler(&ignoreInterrupt, TRUE);
#else
	struct sigaction ignore {};
	ignore.sa_handler = SIG_IGN;
	struct sigaction previousSigInt {}, previousSigQuit {};
	sigaction(SIGINT, &ignore, &previousSigInt);
	sigaction(SIGQUIT, &ignore, &previousSigQuit);
#endif

	process.start();
	const bool started = process.waitForStarted(-1);
	if (started)
	{
		process.waitForFinished(-1);
		if (waitForEnter)
		{
			std::fputs("\nPress Enter to return to File Commander", stdout);
			std::fflush(stdout);
			for (int c = std::getchar(); c != '\n' && c != EOF; c = std::getchar()) {}
		}
	}

#ifdef _WIN32
	SetConsoleCtrlHandler(&ignoreInterrupt, FALSE);
#else
	sigaction(SIGINT, &previousSigInt, nullptr);
	sigaction(SIGQUIT, &previousSigQuit, nullptr);
#endif

	application->resume();
	application->redraw();

	if (!started)
		return std::unexpected(process.errorString());
	return {};
}

void CTuiApplication::copyToClipboard(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
#ifndef _WIN32
	// Turbo Vision tries the local clipboard tools first, which over SSH reach the remote machine's clipboard
	if (!qEnvironmentVariableIsEmpty("SSH_CONNECTION"))
	{
		const QByteArray sequence = "\x1B]52;c;" + utf8.toBase64() + '\a';
		writeToTerminal(sequence);
		return;
	}
#endif
	TClipboard::setText(TStringView{ utf8.constData(), (size_t)utf8.size() });
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
			*new TStatusItem("~F3~ View", kbF3, cmViewFile) +
			*new TStatusItem("~F4~ Edit", kbF4, cmEditFile) +
			*new TStatusItem("~F5~ Copy", kbF5, cmCopyItems) +
			*new TStatusItem("~F6~ Move", kbF6, cmMoveItems) +
			*new TStatusItem("~F7~ Folder", kbF7, cmCreateFolder) +
			*new TStatusItem(nullptr, kbShiftF7, cmCreateFile) +
			*new TStatusItem("~F8~ Delete", kbF8, cmDeleteItems) +
			*new TStatusItem(nullptr, kbShiftF8, cmDeleteItemsPermanently) +
			*new TStatusItem(nullptr, kbF10, cmMenu) +
			*new TStatusItem(nullptr, kbAltX, cmQuit)
	);
}

// The row above the status line. Hints only: the status line binds every key.
TStatusLine* CTuiApplication::initHintLine(TRect r)
{
	r.b.y--;
	r.a.y = r.b.y - 1;
	return new TStatusLine(r,
		*new TStatusDef(0, 0xFFFF) +
			*new TStatusItem("~Tab~ Panel", kbNoKey, 0) +
			*new TStatusItem("~Enter~ Open", kbNoKey, 0) +
			*new TStatusItem("~Alt-D~ Go to", kbNoKey, 0) +
			*new TStatusItem("~Alt-F1/F2~ Volume", kbNoKey, 0) +
			*new TStatusItem("~F10~ Menu", kbNoKey, cmMenu) +
			*new TStatusItem("~Alt-X~ Exit", kbNoKey, cmQuit)
	);
}

TDeskTop* CTuiApplication::initDeskTop(TRect r)
{
	r.a.y++; // The menu bar
	r.b.y -= 2; // The hint line and the status line
	return new TDeskTop(r);
}
