#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>

class CSimpleProgressDialog;

class QWidget;

// Shows a modal, cancellable progress dialog until `finished` returns true, polling it and calling showProgress meanwhile.
// False if the user cancelled.
[[nodiscard]] bool waitWithProgressDialog(QWidget* parent, const QString& title, const QString& label, const std::function<bool()>& finished,
	const std::function<void(CSimpleProgressDialog& dialog)>& showProgress);
