#pragma once

// Helpers shared by all CController test files

#include "ccontroller.h"
#include "settings.h"
#include "temptree.h"

#include "qt_helpers.hpp" // operator<< for QString, so Catch2 can print a path in a failure report


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QSettings>
RESTORE_COMPILER_WARNINGS

// Every test starts as a first run: no saved tabs, so each side restores one tab at the home folder
inline void clearSavedState()
{
	QSettings{}.clear();
}

// A controller restores its tabs from the settings when constructed, and saves them in shutdown(), as on application exit.
// Only one exists at a time: CController is a singleton.
class ControllerSession
{
public:
	ControllerSession() = default;
	~ControllerSession() { _controller.shutdown(); }

	ControllerSession(const ControllerSession&) = delete;
	ControllerSession& operator=(const ControllerSession&) = delete;

	[[nodiscard]] CController* operator->() { return &_controller; }

private:
	CController _controller;
};
