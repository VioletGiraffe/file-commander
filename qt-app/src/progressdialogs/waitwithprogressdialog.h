#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>

class CSimpleProgressDialog;

class QWidget;

// Polls `finished` until it returns true; false if the user cancelled.
// Work still running after 500 ms gets a modal, cancellable progress dialog, which showProgress updates on every poll.
// confirmCancel, if set, decides whether a cancel request cancels; it gets the progress dialog to parent its question.
[[nodiscard]] bool waitWithProgressDialog(QWidget* parent, const QString& title, const QString& label, const std::function<bool()>& finished,
	const std::function<void(CSimpleProgressDialog& dialog)>& showProgress, const std::function<bool(QWidget* dialog)>& confirmCancel = {});
