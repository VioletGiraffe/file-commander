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
class TDeskTop;
class TMenuBar;
class TStatusLine;

class CTuiApplication final : public TApplication
{
public:
	explicit CTuiApplication(CController& controller);

	// Delivers the core's queued work and Qt's events before every Turbo Vision event, so modal views get them too
	void getEvent(TEvent& event) override;
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

	// Searches from the active panel's folder or selection, and goes to the result chosen
	void findFiles();
	// Goes to the difference chosen, in the panel that holds it
	void compareFolders();
	// Recolours the screen and marks the theme in the Theme menu
	void setColourTheme(const ColourTheme& theme);

private:
	CController& _controller;
	TStatusLine* _hintLine; // Owned by this application
	std::array<CTuiPanel*, 2> _panels; // Left and right, owned by the desktop
	CTuiFindDialog* _findDialog = nullptr; // Created on first use and owned by this application: it keeps the last search's results
	CTuiFolderComparison _folderComparison{ _controller };
};
