#pragma once

// Submodule includes
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

	// Shows the box over the bottom of its parent, focused, with the text selected
	void showForEditing();

	// The Esc shortcut is enabled only while shown; hiding passes the focus to the parent
	void setVisible(bool visible) override;

signals:
	void filterTextEdited(QString text);
	void filterTextConfirmed(QString text);

protected:
	void keyPressEvent(QKeyEvent* event) override;

	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QShortcut* _escShortcut = nullptr;
};
