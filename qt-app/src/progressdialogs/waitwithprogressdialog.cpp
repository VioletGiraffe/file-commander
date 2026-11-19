#include "waitwithprogressdialog.h"

// Submodule includes
#include "dialogs/csimpleprogressdialog.h"


DISABLE_COMPILER_WARNINGS
#include <QTimer>
RESTORE_COMPILER_WARNINGS

bool waitWithProgressDialog(QWidget* parent, const QString& title, const QString& label, const std::function<bool()>& finished,
	const std::function<void(CSimpleProgressDialog& dialog)>& showProgress)
{
	CSimpleProgressDialog progressDialog{ parent };
	progressDialog.setWindowTitle(title);
	progressDialog.setLabelText(label);
	progressDialog.setCancellable(true);

	QTimer progressTimer;
	QObject::connect(&progressTimer, &QTimer::timeout, &progressDialog, [&progressDialog, &finished, &showProgress] {
		if (finished())
			progressDialog.accept();
		else
			showProgress(progressDialog);
	});
	progressTimer.start(50);

	return progressDialog.exec() == QDialog::Accepted;
}
