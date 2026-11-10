#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TApplication
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <array>

class CController;
class CTuiPanel;
class TMenuBar;
class TStatusLine;

class CTuiApplication final : public TApplication
{
public:
	explicit CTuiApplication(CController& controller);

	// Delivers the core's queued work and Qt's events before every Turbo Vision event, so modal views get them too
	void getEvent(TEvent& event) override;
	void handleEvent(TEvent& event) override;
	void resume() override;

private:
	static TMenuBar* initMenuBar(TRect r);
	static TStatusLine* initStatusLine(TRect r);

private:
	CController& _controller;
	std::array<CTuiPanel*, 2> _panels; // Left and right, owned by the desktop
};
