#pragma once

#include "plugin_export.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class PLUGIN_EXPORT CPluginWindow : public QMainWindow
{
public:
	explicit CPluginWindow(QWidget* parent) noexcept;

	[[nodiscard]] bool autoDeleteOnClose() const;
	void setAutoDeleteOnClose(bool autoDelete);

	// Called before the central widget is embedded into a file panel; the window itself is never shown.
	virtual void configureForQuickView() {}
	// Releases any handle on the viewed file, keeping its content displayed: called before the host deletes, moves or renames it.
	virtual void closeFile() {}
};
