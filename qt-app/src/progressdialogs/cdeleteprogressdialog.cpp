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

	connect (ui->_btnCancel, &QPushButton::clicked, this, &CDeleteProgressDialog::cancelPressed);
	connect (ui->_btnBackground, &QPushButton::clicked, this, &CDeleteProgressDialog::background);
	connect (ui->_btnPause, &QPushButton::clicked, this, &CDeleteProgressDialog::pauseResume);

	setWindowTitle(tr("Deleting..."));

	_eventsProcessTimer.setInterval(100);
	_eventsProcessTimer.start();
	connect(&_eventsProcessTimer, &QTimer::timeout, this, &CDeleteProgressDialog::processEvents);

	_performer->setWatcher(this);
	_performer->start();
}

CDeleteProgressDialog::~CDeleteProgressDialog()
{
	delete ui;
}

void CDeleteProgressDialog::onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float /*filePercentage*/, uint64_t speed, uint32_t secondsRemaining)
{
	ui->_progress->setValue((int)(totalPercentage + 0.5f));
	ui->_lblNumFiles->setText(QString("%1/%2").arg(numFilesProcessed).arg(totalNumFiles));
	static const QString titleTemplate(tr("%1% Deleting... %2 items / second, %3 remaining"));
	setWindowTitle(titleTemplate.arg(QString::number(totalPercentage, 'f', 1)).arg(speed).arg(secondsRemaining));
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
	close();

	if (!message.isEmpty())
		QMessageBox::information(this, tr("Operation finished"), message);
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
	if (!_performer || QMessageBox::question(this, "Cancel?", "Are you sure you want to cancel this operation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		cancel();
}

void CDeleteProgressDialog::pauseResume()
{
	ui->_btnPause->setText(_performer->togglePause() ? tr("Resume") : tr("Pause"));
	ui->_progress->setState(_performer->paused() ? psPaused : psNormal);
}

void CDeleteProgressDialog::background()
{
	ui->_btnBackground->setVisible(false);
	QTimer::singleShot(0, this, &CDeleteProgressDialog::setMinSize);
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
