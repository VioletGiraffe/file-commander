#include "cdeleteprogressdialog.h"
#include "ui_cdeleteprogressdialog.h"
#include "../cmainwindow.h"
#include "cpromptdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QCloseEvent>
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

CDeleteProgressDialog::CDeleteProgressDialog(std::vector<CFileSystemObject> source, QString destination, CMainWindow *mainWindow) :
	QWidget(0, Qt::Window),
	ui(new Ui::CDeleteProgressDialog),
	_performer(new COperationPerformer(operationDelete, source, destination)),
	_mainWindow(mainWindow)
{
	ui->setupUi(this);
	ui->_progress->linkToWidgetstaskbarButton(this);

	assert(mainWindow);

	ui->_lblNumFiles->clear();
	ui->_lblFileName->clear();

	connect (ui->_btnCancel,     SIGNAL(clicked()), SLOT(cancelPressed()));
	connect (ui->_btnBackground, SIGNAL(clicked()), SLOT(background()));
	connect (ui->_btnPause,      SIGNAL(clicked()), SLOT(pauseResume()));

	setWindowTitle("Deleting...");

	_eventsProcessTimer.setInterval(100);
	_eventsProcessTimer.start();
	connect(&_eventsProcessTimer, SIGNAL(timeout()), SLOT(processEvents()));

	_performer->setWatcher(this);
	_performer->start();
}

CDeleteProgressDialog::~CDeleteProgressDialog()
{
	if (_performer)
	{
		_performer->cancel();
		delete _performer;
	}
	delete ui;
}

void CDeleteProgressDialog::onProgressChanged(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int /*filePercentage*/, uint64_t /*speed*/)
{
	ui->_progress->setValue(totalPercentage);
	ui->_lblNumFiles->setText(QString("%1/%2").arg(numFilesProcessed).arg(totalNumFiles));
	static const QString titleTemplate ("%1% Deleting....");
	setWindowTitle(titleTemplate.arg(totalPercentage));
}

void CDeleteProgressDialog::onProcessHalted(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage)
{
	CPromptDialog prompt(this, operationDelete, reason, source, dest, errorMessage);

	ui->_progress->setState(psStopped);

	const UserResponse response = prompt.ask();
	_performer->userResponse(reason, response, response == urRename ? prompt.newName() : QString());
	ui->_progress->setState(_performer->paused() ? psPaused : psNormal);
}

void CDeleteProgressDialog::onProcessFinished(QString message)
{
	delete _performer;
	_performer = 0;
	close();

	if (!message.isEmpty())
		QMessageBox::information(this, "Operation finished", message);
}

void CDeleteProgressDialog::onCurrentFileChanged(QString file)
{
	ui->_lblFileName->setText(file);
}

void CDeleteProgressDialog::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close && _performer)
	{
		if (QMessageBox::question(this, "Abort?", "Do you want to abort the operation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		{
			cancel();
			QWidget::closeEvent(e);
		}
	}
}

void CDeleteProgressDialog::cancelPressed()
{
	if (QMessageBox::question(this, "Cancel?", "Are you sure you want to cancel this operation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		cancel();
}

void CDeleteProgressDialog::pauseResume()
{
	ui->_btnPause->setText(_performer->togglePause() ? "Resume" : "Pause");
	ui->_progress->setState(_performer->paused() ? psPaused : psNormal);
}

void CDeleteProgressDialog::background()
{
	ui->_btnBackground->setVisible(false);
	QTimer::singleShot(0, this, SLOT(setMinSize()));
}

void CDeleteProgressDialog::setMinSize()
{
	setGeometry(QRect(geometry().topLeft(), QPoint(geometry().topLeft().x() + minimumSize().width(), geometry().topLeft().y() + minimumSize().height())));
	_mainWindow->raise();
	_mainWindow->activateWindow();
}

void CDeleteProgressDialog::processEvents()
{
	std::lock_guard<std::mutex> lock(_callbackMutex);
	for (auto event = _callbacks.begin(); event != _callbacks.end(); ++event)
		(*event)();
	_callbacks.clear();
}

void CDeleteProgressDialog::cancel()
{
	_performer->cancel();
	ui->_btnCancel->setEnabled(false);
	ui->_btnPause->setEnabled(false);
}
