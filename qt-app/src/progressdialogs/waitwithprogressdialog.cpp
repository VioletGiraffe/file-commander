#include "waitwithprogressdialog.h"

// Submodule includes
#include "dialogs/csimpleprogressdialog.h"


DISABLE_COMPILER_WARNINGS
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

namespace {

// Esc, the Cancel button and the close button all reach reject(), which cancels only if confirmCancel agrees
class CConfirmingProgressDialog final : public CSimpleProgressDialog
{
public:
	CConfirmingProgressDialog(QWidget* parent, const std::function<bool(QWidget* dialog)>& confirmCancel) :
		CSimpleProgressDialog{ parent },
		_confirmCancel{ confirmCancel }
	{}

	[[nodiscard]] bool confirmingCancel() const noexcept { return _confirmingCancel; }

	void reject() override
	{
		if (_confirmCancel)
		{
			_confirmingCancel = true;
			const bool cancel = _confirmCancel(this);
			_confirmingCancel = false;
			if (!cancel)
				return;
		}

		CSimpleProgressDialog::reject();
	}

protected:
	void closeEvent(QCloseEvent* e) override
	{
		e->ignore();
		reject();
	}

private:
	const std::function<bool(QWidget* dialog)>& _confirmCancel;
	bool _confirmingCancel = false;
};

} // namespace

bool waitWithProgressDialog(QWidget* parent, const QString& title, const QString& label, const std::function<bool()>& finished,
	const std::function<void(CSimpleProgressDialog& dialog)>& showProgress, const std::function<bool(QWidget* dialog)>& confirmCancel)
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

	CConfirmingProgressDialog progressDialog{ parent, confirmCancel };
	progressDialog.setWindowTitle(title);
	progressDialog.setLabelText(label);
	progressDialog.setCancellable(true);

	QTimer progressTimer;
	QObject::connect(&progressTimer, &QTimer::timeout, &progressDialog, [&progressDialog, &finished, &showProgress] {
		// The question stays on top of the dialog until it is answered
		if (progressDialog.confirmingCancel())
			return;

		if (finished())
			progressDialog.accept();
		else
			showProgress(progressDialog);
	});
	progressTimer.start(50);

	return progressDialog.exec() == QDialog::Accepted;
}
