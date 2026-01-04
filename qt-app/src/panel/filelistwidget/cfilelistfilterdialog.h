#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QLineEdit>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFileListFilterDialog;
}

class QShortcut;

class CFileListFilterDialog final : public QLineEdit
{
	Q_OBJECT
public:
	explicit CFileListFilterDialog(QWidget *parent);

	void showAt(const QPoint& bottomLeft);

signals:
	void filterTextEdited(QString text);
	void filterTextConfirmed(QString text);

protected:
	void keyPressEvent(QKeyEvent* event) override;

	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QShortcut* _escShortcut = nullptr;
};
