#include "cdeleteprogressdialog.h"
#include "../cmainwindow.h"
#include "cpromptdialog.h"
#include "progressdialoghelpers.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cdeleteprogressdialog.h"

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

	ui->_lblOperationNameAndSpeed->clear();
	ui->_lblFileName->clear();

	connect (ui->_btnCancel, &QPushButton::clicked, this, &CDeleteProgressDialog::cancelPressed);
	connect (ui->_btnBackground, &QPushButton::clicked, this, &CDeleteProgressDialog::background);
	connect (ui->_btnPause, &QPushButton::clicked, this, &CDeleteProgressDialog::pauseResume);

	setWindowTitle(tr("Deleting..."));

	_eventsProcessTimer.setInterval(100);
	_eventsProcessTimer.start();
	connect(&_eventsProcessTimer, &QTimer::timeout, this, [this]() {processEvents(); });

	_performer->setObserver(this);
	_performer->start();
}

CDeleteProgressDialog::~CDeleteProgressDialog()
{
	delete ui;
}

void CDeleteProgressDialog::onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float /*filePercentage*/, uint64_t speed, uint32_t secondsRemaining)
{
	ui->_progress->setValue((int)(totalPercentage + 0.5f));
	ui->_lblOperationNameAndSpeed->setText(tr("Deleting item %1 of %2, %3 items / second, %4 remaining").arg(numFilesProcessed).arg(totalNumFiles).arg(speed).arg(secondsToTimeIntervalString(secondsRemaining)));
	static const QString titleTemplate(tr("%1% Deleting... %2 items / second, %3 remaining"));
	setWindowTitle(titleTemplate.arg(QString::number(totalPercentage, 'f', 1)).arg(speed).arg(secondsToTimeIntervalString(secondsRemaining)));
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
	if (!_performer->done() && e->type() == QCloseEvent::Close)
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
	ui->_btnPause->setText(_performer->togglePause() ? tr("Resume") : tr("Pause"));
	ui->_progress->setState(_performer->paused() ? psPaused : psNormal);
}

void CDeleteProgressDialog::background()
{
	ui->_btnBackground->setVisible(false);
	QTimer::singleShot(0, [this](){
		setGeometry(QRect(geometry().topLeft(), QPoint(geometry().topLeft().x() + minimumSize().width(), geometry().topLeft().y() + minimumSize().height())));
		_mainWindow->raise();
		_mainWindow->activateWindow();
	});
}

void CDeleteProgressDialog::cancel()
{
	_performer->cancel();
	ui->_btnCancel->setEnabled(false);
	ui->_btnPause->setEnabled(false);
}
