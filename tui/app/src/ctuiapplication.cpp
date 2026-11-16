#include "ctuiapplication.h"
#include "ctuicolourtheme.h"
#include "ctuifinddialog.h"
#include "ctuipalette.h"
#include "ctuipanel.h"
#include "ctuiviewer.h"

#include "ccontroller.h"
#include "cfilesystemobject.h"
#include "filesearchengine/filesearchquery.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TClipboard
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TMenu
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
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

#define KEY_TUI_COLOUR_THEME QStringLiteral("TerminalUI/ColourTheme")

namespace
{
// Carried out by the application, not the focused panel
constexpr ushort cmChooseLeftPanelVolume = 1100, cmChooseRightPanelVolume = 1101, cmFindFiles = 1102, cmCompareFolders = 1103;
// cmFirstColourTheme + n chooses colourThemes()[n]
constexpr ushort cmFirstColourTheme = 1104;

[[nodiscard]] bool isColourThemeCommand(ushort command)
{
	return command >= cmFirstColourTheme && command < cmFirstColourTheme + colourThemes().size();
}

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

class CTuiHintLine final : public TStatusLine
{
public:
	using TStatusLine::TStatusLine;

	TPalette& getPalette() const override { return hintLinePalette(); }
};

// Shows each item as a block, with a gap between blocks
class CTuiKeyBar final : public TStatusLine
{
public:
	using TStatusLine::TStatusLine;

	void draw() override { drawItems(nullptr); }
	TPalette& getPalette() const override { return keyBarPalette(); }

	// Handles clicks itself: the base class's hit test and pressed highlight assume its own layout, which has no gaps
	void handleEvent(TEvent& event) override
	{
		if (event.what != evMouseDown)
		{
			TStatusLine::handleEvent(event);
			return;
		}

		TView::handleEvent(event);
		const TStatusItem* pressed = nullptr;
		do
		{
			if (const TStatusItem* hovered = itemAt(makeLocal(event.mouse.where)); hovered != pressed)
				drawItems(pressed = hovered);
		} while (mouseEvent(event, evMouseMove));

		if (pressed && commandEnabled(pressed->command))
		{
			event.what = evCommand;
			event.message.command = pressed->command;
			event.message.infoPtr = nullptr;
			putEvent(event);
		}
		clearEvent(event);
		drawView();
	}

private:
	static constexpr ushort gapWidth = 2;

	// Calls function(item, x, width) for each item with a label that fits, left to right
	template <typename Function>
	void forEachShownItem(Function&& function) const
	{
		ushort x = 0;
		for (const TStatusItem* item = items; item; item = item->next)
		{
			if (!item->text) // A key binding only
				continue;

			const ushort width = (ushort)cstrlen(item->text);
			if (x + width > size.x)
				return;

			function(*item, x, width);
			x = (ushort)(x + width + gapWidth);
		}
	}

	[[nodiscard]] const TStatusItem* itemAt(TPoint point) const
	{
		const TStatusItem* found = nullptr;
		if (point.y == 0)
		{
			forEachShownItem([&](const TStatusItem& item, ushort x, ushort width) {
				if (point.x >= x && point.x < x + width)
					found = &item;
			});
		}
		return found;
	}

	void drawItems(const TStatusItem* pressed)
	{
		TDrawBuffer line;
		line.moveChar(0, ' ', getColor(7), (ushort)size.x);
		forEachShownItem([&](const TStatusItem& item, ushort x, ushort) {
			const bool enabled = commandEnabled(item.command);
			// Low byte: the label's entry; high byte: the key's
			const TAttrPair colors = &item == pressed ? getColor(enabled ? 0x0604 : 0x0505) : getColor(enabled ? 0x0301 : 0x0202);
			line.moveCStr(x, item.text, colors);
		});
		writeLine(0, 0, (short)size.x, 1, line);
	}
};

class CTuiMenuBar final : public TMenuBar
{
public:
	using TMenuBar::TMenuBar;

	// Names the Theme menu's items, with a mark on the theme given
	void markColourTheme(const ColourTheme& theme) { markColourTheme(menu, theme); }

private:
	static void markColourTheme(TMenu* someMenu, const ColourTheme& theme)
	{
		for (TMenuItem* item = someMenu->items; item; item = item->next)
		{
			if (item->command == 0 && item->subMenu) // A separator has no submenu
				markColourTheme(item->subMenu, theme);
			else if (isColourThemeCommand(item->command))
			{
				const ColourTheme& itemTheme = colourThemes()[item->command - cmFirstColourTheme];
				const std::string itemName = (&itemTheme == &theme ? "• " : "  ") + std::string{ itemTheme.menuName };
				delete[] item->name;
				item->name = newStr(itemName.c_str());
			}
		}
	}
};

// A group draws its subviews again only when it has no buffer
void freeDrawBuffers(TGroup& group)
{
	group.freeBuffer();
	group.forEach([](TView* view, void*) {
		if (auto* subgroup = dynamic_cast<TGroup*>(view))
			freeDrawBuffers(*subgroup);
	}, nullptr);
}

[[nodiscard]] TMenu* newColourThemeMenu()
{
	const std::span<const ColourTheme> themes = colourThemes();
	TMenuItem* items = nullptr;
	for (size_t i = themes.size(); i-- > 0;)
	{
		if (items && themes[i].polarity != themes[i + 1].polarity)
			items = &(newLine() + *items);
		items = new TMenuItem(themes[i].menuName, (ushort)(cmFirstColourTheme + i), kbNoKey, hcNoContext, {}, items);
	}
	return new TMenu(*items);
}
} // namespace

CTuiApplication::CTuiApplication(CController& controller) :
	TProgInit(&CTuiApplication::initStatusLine, &CTuiApplication::initMenuBar, &CTuiApplication::initDeskTop),
	_controller(controller)
{
	disableWin32InputMode();
	_hintLine = initHintLine(getExtent());
	insert(_hintLine);

	_panels = {
		new CTuiPanel(deskTop->size, _controller, Panel::LeftPanel),
		new CTuiPanel(deskTop->size, _controller, Panel::RightPanel)
	};
	for (CTuiPanel* panel : _panels)
		deskTop->insert(panel);
	_panels[(size_t)Panel::LeftPanel]->select();

	const std::string themeId = QSettings{}.value(KEY_TUI_COLOUR_THEME).toString().toStdString();
	const std::span<const ColourTheme> themes = colourThemes();
	const auto savedTheme = std::ranges::find_if(themes, [&themeId](const ColourTheme& theme) { return themeId == theme.id; });
	setColourTheme(savedTheme != themes.end() ? *savedTheme : themes.front());
}

void CTuiApplication::getEvent(TEvent& event)
{
	QCoreApplication::processEvents();
	_controller.uiThreadTimerTick();
	TApplication::getEvent(event);
}

void CTuiApplication::idle()
{
	TApplication::idle();
	_hintLine->update(); // The base class updates only its own status line, the key bar
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

	// The volume of the left or the right panel, from either panel
	if (event.what == evCommand && (event.message.command == cmChooseLeftPanelVolume || event.message.command == cmChooseRightPanelVolume))
	{
		CTuiPanel* panel = _panels[(size_t)(event.message.command == cmChooseLeftPanelVolume ? Panel::LeftPanel : Panel::RightPanel)];
		clearEvent(event);
		panel->select();
		panel->chooseVolume();
		return;
	}

	if (event.what == evCommand && event.message.command == cmFindFiles)
	{
		clearEvent(event);
		findFiles();
		return;
	}

	if (event.what == evCommand && event.message.command == cmCompareFolders)
	{
		clearEvent(event);
		compareFolders();
		return;
	}

	if (event.what == evCommand && isColourThemeCommand(event.message.command))
	{
		const ColourTheme& theme = colourThemes()[event.message.command - cmFirstColourTheme];
		clearEvent(event);
		setColourTheme(theme);
		QSettings{}.setValue(KEY_TUI_COLOUR_THEME, QString::fromLatin1(theme.id));
		return;
	}

	TApplication::handleEvent(event);
}

void CTuiApplication::setColourTheme(const ColourTheme& theme)
{
	applyColourTheme(theme);
	static_cast<CTuiMenuBar*>(menuBar)->markColourTheme(theme); // initMenuBar creates a CTuiMenuBar

	// Buffers drawn in the old colours: the desktop holds the panels and every open window, and the closed Find dialog is in no group
	freeDrawBuffers(*deskTop);
	if (_findDialog)
		freeDrawBuffers(*_findDialog);
	redraw();
}

void CTuiApplication::findFiles()
{
	if (!_findDialog)
		_findDialog = new CTuiFindDialog(deskTop->getExtent());

	const Panel side = _controller.activePanelPosition();
	const CPanel& panel = _controller.panel(side);
	const CTuiFindDialog::Outcome outcome = _findDialog->exec(searchInFieldTextFor(panel), panel.currentDirPathPosix());
	if (const auto* goTo = std::get_if<CTuiFindDialog::GoToResult>(&outcome))
		_controller.goToItem(side, CFileSystemObject{ goTo->path });
	else if (const auto* send = std::get_if<CTuiFindDialog::SendResultsToPanel>(&outcome))
		_controller.showSearchResults(side, send->paths);
}

void CTuiApplication::compareFolders()
{
	const std::optional<CFolderComparison::EntryLocation> location = _folderComparison.run();
	if (!location)
		return;

	_panels[(size_t)location->panel]->select();
	_controller.goToItem(location->panel, CFileSystemObject{ location->absolutePath });
}

void CTuiApplication::resume()
{
	TApplication::resume();
	disableWin32InputMode();
}

void CTuiApplication::shutDown()
{
	// No group owns the dialog while it is closed
	if (_findDialog)
		TObject::destroy(std::exchange(_findDialog, nullptr));

	TApplication::shutDown();
}

TPalette& CTuiApplication::getPalette() const
{
	return applicationPalette();
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
	// Menu titles avoid the letters of the panel's Alt+letter keys: Alt plus a title's letter opens that menu.
	// Keys a text field uses stay with the panel (see commandForPanelKey), so their menu items only show them.
	return new CTuiMenuBar(r,
		*new TSubMenu("~F~ile", kbAltF) +
			*new TMenuItem("~R~ename...", cmRenameItem, kbF2, hcNoContext, "F2") +
			*new TMenuItem("~B~atch rename...", cmBatchRename, kbShiftF2, hcNoContext, "Shift-F2") +
			*new TMenuItem("~V~iew", cmViewFile, kbF3, hcNoContext, "F3") +
			*new TMenuItem("~E~dit", cmEditFile, kbF4, hcNoContext, "F4") +
			*new TMenuItem("~C~opy...", cmCopyItems, kbF5, hcNoContext, "F5") +
			*new TMenuItem("~M~ove...", cmMoveItems, kbF6, hcNoContext, "F6") +
			*new TMenuItem("New ~f~older...", cmCreateFolder, kbF7, hcNoContext, "F7") +
			*new TMenuItem("New f~i~le...", cmCreateFile, kbShiftF7, hcNoContext, "Shift-F7") +
			*new TMenuItem("~D~elete...", cmDeleteItems, kbF8, hcNoContext, "F8") +
			*new TMenuItem("Delete ~p~ermanently...", cmDeleteItemsPermanently, kbShiftF8, hcNoContext, "Shift-F8") +
			newLine() +
			*new TMenuItem("Fi~n~d files...", cmFindFiles, kbAltF7, hcNoContext, "Alt-F7") +
			*new TMenuItem("Calculate ~o~ccupied space", cmCalculateOccupiedSpace, kbCtrlL, hcNoContext, "Ctrl-L") +
			*new TMenuItem("Calculate each folder's si~z~e", cmCalculateEachFolderSize, TKey(kbCtrlL, kbShift), hcNoContext, "Ctrl-Shift-L") +
			*new TMenuItem("Comp~a~re folders...", cmCompareFolders, kbNoKey) +
			*new TMenuItem("Copy pat~h~s", cmCopyPaths, kbAltC, hcNoContext, "Alt-C") +
			*new TMenuItem("Open ~s~hell here", cmOpenShell, kbCtrlO, hcNoContext, "Ctrl-O") +
			newLine() +
			*new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X") +
		*new TSubMenu("~S~election", kbAltS) +
			*new TMenuItem("~T~oggle selection", cmToggleSelectionAndMoveDown, kbNoKey, hcNoContext, "Ins") +
			*new TMenuItem("~S~elect by pattern...", cmSelectByPattern, kbNoKey, hcNoContext, "+") +
			*new TMenuItem("~D~eselect by pattern...", cmDeselectByPattern, kbNoKey, hcNoContext, "-") +
			*new TMenuItem("Select by ~e~xtension", cmSelectByExtension, kbNoKey, hcNoContext, "=") +
			*new TMenuItem("~I~nvert selection", cmInvertSelection, kbNoKey, hcNoContext, "*") +
		*new TSubMenu("~T~abs", kbAltT) +
			*new TMenuItem("~N~ew tab", cmNewTab, kbCtrlT, hcNoContext, "Ctrl-T") +
			*new TMenuItem("~C~lose tab", cmCloseTab, kbCtrlW, hcNoContext, "Ctrl-W") +
			*new TMenuItem("~R~eopen closed tab", cmReopenClosedTab, kbNoKey) +
			*new TMenuItem("~D~uplicate tab", cmDuplicateTab, kbNoKey) +
			*new TMenuItem("Close ~a~ll other tabs", cmCloseOtherTabs, kbNoKey) +
			newLine() +
			*new TMenuItem("Ne~x~t tab", cmNextTab, kbCtrlPgDn, hcNoContext, "Ctrl-PgDn") +
			*new TMenuItem("~P~revious tab", cmPreviousTab, kbCtrlPgUp, hcNoContext, "Ctrl-PgUp") +
			*new TMenuItem("~F~irst tab", cmActivateTab1, kbNoKey, hcNoContext, "Alt-1..9") +
			newLine() +
			*new TMenuItem("~O~pen folder in new tab", cmOpenFolderInNewTab, kbCtrlUp, hcNoContext, "Ctrl-Up") +
		*new TSubMenu("~V~iew", kbAltV) +
			*new TMenuItem("Toggle ~h~idden files", cmToggleHiddenFiles, kbCtrlS, hcNoContext, "Ctrl-S") +
			*new TMenuItem("~F~ilter", cmOpenFilterLine, kbCtrlF, hcNoContext, "Ctrl-F") +
			*new TMenuItem("Show ~a~ll files below", cmShowAllFilesBelow, kbCtrlB, hcNoContext, "Ctrl-B") +
			*new TMenuItem("~R~efresh", cmRefresh, kbCtrlR, hcNoContext, "Ctrl-R") +
			newLine() +
			*new TMenuItem("Sort by ~n~ame", cmSortByName, kbCtrlF3, hcNoContext, "Ctrl-F3") +
			*new TMenuItem("Sort by ~e~xtension", cmSortByExtension, kbCtrlF4, hcNoContext, "Ctrl-F4") +
			*new TMenuItem("Sort by ~d~ate", cmSortByDate, kbCtrlF5, hcNoContext, "Ctrl-F5") +
			*new TMenuItem("Sort by ~s~ize", cmSortBySize, kbCtrlF6, hcNoContext, "Ctrl-F6") +
			newLine() +
			*new TMenuItem("~T~heme", kbNoKey, newColourThemeMenu()) +
		*new TSubMenu("~G~o", kbAltG) +
			*new TMenuItem("~B~ack", cmGoBack, kbAltLeft, hcNoContext, "Alt-Left") +
			*new TMenuItem("~F~orward", cmGoForward, kbAltRight, hcNoContext, "Alt-Right") +
			*new TMenuItem("~U~p", cmGoUp, kbNoKey, hcNoContext, "Backspace") +
			*new TMenuItem("~J~ump to first file", cmJumpToFirstFile, kbCtrlHome, hcNoContext, "Ctrl-Home") +
			*new TMenuItem("~G~o to folder...", cmGoToFolder, kbAltD, hcNoContext, "Alt-D") +
			*new TMenuItem("Go to file lo~c~ation", cmGoToFileLocation, kbCtrlG, hcNoContext, "Ctrl-G") +
			*new TMenuItem("Folder ~h~istory...", cmShowFolderHistory, kbAltF12, hcNoContext, "Alt-F12") +
			*new TMenuItem("F~a~vorites...", cmShowFavorites, kbCtrlD, hcNoContext, "Ctrl-D") +
			newLine() +
			*new TMenuItem("~L~eft panel volume...", cmChooseLeftPanelVolume, kbAltF1, hcNoContext, "Alt-F1") +
			*new TMenuItem("~R~ight panel volume...", cmChooseRightPanelVolume, kbAltF2, hcNoContext, "Alt-F2")
	);
}

// The menus bind F2 to F8: their items here respond to clicks only.
// Binds F10, which opens the menus, and Alt+1 to Alt+9: a menu item binds only one key.
// The viewer binds its own keys.
TStatusLine* CTuiApplication::initStatusLine(TRect r)
{
	r.a.y = r.b.y - 1;
	static_assert(tabPositionCommandCount == 9);
	return new CTuiKeyBar(r,
		*new TStatusDef(hcViewer, hcViewer) +
			*new TStatusItem("~Ctrl-F~ Find", kbNoKey, cmFindInViewer) +
			*new TStatusItem("~F3~ Next", kbNoKey, cmFindNextInViewer) +
			*new TStatusItem("~Shift-F3~ Previous", kbNoKey, cmFindPreviousInViewer) +
			*new TStatusItem("~Shift-W~ Wrap", kbNoKey, cmToggleLineWrap) +
			*new TStatusItem("~Esc~ Close", kbNoKey, cmCancel) +
		*new TStatusDef(0, 0xFFFF) +
			*new TStatusItem("~F2~ Rename", kbNoKey, cmRenameItem) +
			*new TStatusItem("~F3~ View", kbNoKey, cmViewFile) +
			*new TStatusItem("~F4~ Edit", kbNoKey, cmEditFile) +
			*new TStatusItem("~F5~ Copy", kbNoKey, cmCopyItems) +
			*new TStatusItem("~F6~ Move", kbNoKey, cmMoveItems) +
			*new TStatusItem("~F7~ Folder", kbNoKey, cmCreateFolder) +
			*new TStatusItem("~F8~ Delete", kbNoKey, cmDeleteItems) +
			*new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
			*new TStatusItem(nullptr, kbAlt1, cmActivateTab1) +
			*new TStatusItem(nullptr, kbAlt2, cmActivateTab1 + 1) +
			*new TStatusItem(nullptr, kbAlt3, cmActivateTab1 + 2) +
			*new TStatusItem(nullptr, kbAlt4, cmActivateTab1 + 3) +
			*new TStatusItem(nullptr, kbAlt5, cmActivateTab1 + 4) +
			*new TStatusItem(nullptr, kbAlt6, cmActivateTab1 + 5) +
			*new TStatusItem(nullptr, kbAlt7, cmActivateTab1 + 6) +
			*new TStatusItem(nullptr, kbAlt8, cmActivateTab1 + 7) +
			*new TStatusItem(nullptr, kbAlt9, cmActivateTab1 + 8)
	);
}

// The row above the status line. Hints only: it binds no keys.
TStatusLine* CTuiApplication::initHintLine(TRect r)
{
	r.b.y--;
	r.a.y = r.b.y - 1;
	return new CTuiHintLine(r,
		*new TStatusDef(hcViewer, hcViewer) +
			*new TStatusItem("View as:", kbNoKey, 0) +
			*new TStatusItem("~1~ Auto", kbNoKey, cmViewAs(ViewAs::DetectedEncoding)) +
			*new TStatusItem("~2~ Latin-1", kbNoKey, cmViewAs(ViewAs::Latin1)) +
			*new TStatusItem("~3~ System", kbNoKey, cmViewAs(ViewAs::SystemLocale)) +
			*new TStatusItem("~4~ UTF-8", kbNoKey, cmViewAs(ViewAs::Utf8)) +
			*new TStatusItem("~5~ UTF-16", kbNoKey, cmViewAs(ViewAs::Utf16)) +
			*new TStatusItem("~6~ Hex", kbNoKey, cmViewAs(ViewAs::Hex)) +
		*new TStatusDef(0, 0xFFFF) +
			*new TStatusItem("~Alt-D~ Go to", kbNoKey, cmGoToFolder) +
			*new TStatusItem("~Alt-F1/F2~ Volume", kbNoKey, 0) +
			*new TStatusItem("~Alt-F7~ Find", kbNoKey, cmFindFiles) +
			*new TStatusItem("~Shift-F2~ Batch rename", kbNoKey, cmBatchRename) +
			*new TStatusItem("~Alt-X~ Exit", kbNoKey, cmQuit)
	);
}

TDeskTop* CTuiApplication::initDeskTop(TRect r)
{
	r.a.y++; // The menu bar
	r.b.y -= 2; // The hint line and the status line
	return new TDeskTop(r);
}
