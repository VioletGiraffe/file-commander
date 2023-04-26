#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CNewFavoriteLocationDialog;
}

class CNewFavoriteLocationDialog final : public QDialog
{
public:
	explicit CNewFavoriteLocationDialog(QWidget *parent, bool subcategory);
	~CNewFavoriteLocationDialog() override;

	[[nodiscard]] QString name() const;
	[[nodiscard]] QString location() const;

private:
	Ui::CNewFavoriteLocationDialog *ui;
};
