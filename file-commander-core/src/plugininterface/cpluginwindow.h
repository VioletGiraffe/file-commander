#pragma once

#include "plugin_export.h"
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
};
