#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

class CFileOperationDialogBase : public QWidget
{
public:
	using QWidget::QWidget;
	inline virtual ~CFileOperationDialogBase() noexcept = default;

	inline bool isInBackgroundMode() const noexcept { return _isInBackroundMode; }

protected:
	bool _isInBackroundMode = false;
};
