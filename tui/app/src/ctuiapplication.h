#pragma once

#include "ctuifoldercomparison.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TApplication
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <expected>

struct ColourTheme;
class CController;
class CTuiFindDialog;
class CTuiPanel;
class CTuiQuickView;
class TDeskTop;
class TMenuBar;
class TStatusLine;

class CTuiApplication final : public TApplication
{
public:
	explicit CTuiApplication(CController& controller);

	// Delivers the core's queued work and Qt's events before every Turbo Vision event, so modal views get them too.
	// Not before a queued event: a dialog that work opens would take it.
	void getEvent(TEvent& event) override;
	// True while an event queued with putEvent waits: the next getEvent returns it before reading input
	[[nodiscard]] static bool hasPendingEvent() noexcept { return pending.what != evNothing; }
	// The hint row and the key bar show the items of the top view's help context
	void idle() override;
	void handleEvent(TEvent& event) override;
	void resume() override;
	void shutDown() override;
	TPalette& getPalette() const override;

	// Hands the terminal to the program until it exits. The error: the program did not start.
	// waitForEnter: the program's output stays on screen until Enter is pressed
	[[nodiscard]] static std::expected<void, QString> runInTerminal(const QString& program, const QStringList& arguments, const QString& workingDir, bool waitForEnter);
	// The clipboard of the computer the user types on: over SSH the terminal's (OSC 52), otherwise Turbo Vision's choice
	static void copyToClipboard(const QString& text);

private:
	static TMenuBar* initMenuBar(TRect r);
	static TStatusLine* initStatusLine(TRect r);
	static TStatusLine* initHintLine(TRect r);
	static TDeskTop* initDeskTop(TRect r);

	// False for a command the application does not carry out
	bool runCommand(ushort command);
	// Focuses the side and lists the volumes for it, from either panel
	void chooseVolumeOn(Panel side);
	// Searches from the active panel's folder or selection, and goes to the result chosen
	void findFiles();
	// Goes to the difference chosen, in the panel that holds it
	void compareFolders();
	// Over the other panel than the active one, which keeps the focus
	void toggleQuickView();
	// Before the panel under it is used again
	void endQuickViewOver(Panel side);
	// The file under the side's cursor, while quick view covers the other side
	void showCurrentFileInQuickView(Panel side);
	// A list of the themes that shows each one on the whole screen while the cursor is on it. Saves the theme chosen.
	void chooseColourTheme();
	// Recolours the screen
	void setColourTheme(const ColourTheme& theme);

private:
	CController& _controller;
	TStatusLine* _hintLine; // Owned by this application
	std::array<CTuiPanel*, 2> _panels; // Left and right, owned by the desktop
	CTuiFindDialog* _findDialog = nullptr; // Created on first use and owned by this application: it keeps the last search's results
	CTuiFolderComparison _folderComparison{ _controller };
	CTuiQuickView* _quickView = nullptr; // While quick view is on; owned by the desktop
};
