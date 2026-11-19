#include "waitwithprogressdialog.h"

// Submodule includes
#include "dialogs/csimpleprogressdialog.h"


DISABLE_COMPILER_WARNINGS
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

bool waitWithProgressDialog(QWidget* parent, const QString& title, const QString& label, const std::function<bool()>& finished,
	const std::function<void(CSimpleProgressDialog& dialog)>& showProgress)
{
	// Work that ends within this time shows no dialog. Input waits until then; painting does not.
	{
		static constexpr int dialogDelayMs = 500;
		QElapsedTimer sinceStart;
		sinceStart.start();

		QEventLoop loop;
		QTimer pollTimer;
		QObject::connect(&pollTimer, &QTimer::timeout, &loop, [&loop, &sinceStart, &finished] {
			if (finished() || sinceStart.elapsed() >= dialogDelayMs)
				loop.quit();
		});
		pollTimer.start(10);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}

	if (finished())
		return true;

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
