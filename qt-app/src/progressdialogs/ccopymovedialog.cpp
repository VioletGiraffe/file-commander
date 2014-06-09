#include "ccopymovedialog.h"
#include "ui_ccopymovedialog.h"
#include "../cmainwindow.h"
#include "cpromptdialog.h"

CCopyMoveDialog::CCopyMoveDialog(Operation operation, std::vector<CFileSystemObject> source, QString destination, CMainWindow * mainWindow) :
	QWidget(0, Qt::Window),
	ui(new Ui::CCopyMoveDialog),
	_performer(new COperationPerformer(operation, source, destination)),
	_mainWindow(mainWindow),
	_op(operation),
	_titleTemplate(_op == operationCopy ? "%1% Copying %2/s" : "%1% Moving %2/s"),
	_labelTemplate(_op == operationCopy ? "Copying files...  %2/s" : "Moving files...  %2/s"),
	_speed(0)
{
	ui->setupUi(this);
	ui->_overallProgress->linkToWidgetstaskbarButton(this);

	ui->_lblFileName->clear();

	assert(mainWindow);

	if (operation == operationCopy)
		ui->_lblOperationName->setText("Copying files...");
	else if (operation == operationMove)
		ui->_lblOperationName->setText("Moving files...");
	else
		assert(false);

	connect (ui->_btnCancel,     SIGNAL(clicked()), SLOT(cancelPressed()));
	connect (ui->_btnBackground, SIGNAL(clicked()), SLOT(background()));
	connect (ui->_btnPause,      SIGNAL(clicked()), SLOT(pauseResume()));

	setWindowTitle(ui->_lblOperationName->text());

	_eventsProcessTimer.setInterval(100);
	_eventsProcessTimer.start();
	connect(&_eventsProcessTimer, SIGNAL(timeout()), SLOT(processEvents()));

	_performer->setWatcher(this);
	_performer->start();
}

CCopyMoveDialog::~CCopyMoveDialog()
{
	if (_performer)
	{
		_performer->cancel();
		delete _performer;
	}
	delete ui;
}

void CCopyMoveDialog::onProgressChanged(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int filePercentage, uint64_t speed)
{
	if (speed > 0)
		_speed = speed;
	ui->_overallProgress->setValue(totalPercentage);
	ui->_fileProgress->setValue(filePercentage);
	ui->_lblOperationName->setText(_labelTemplate.arg(fileSizeToString(_speed)));
	ui->_lblNumFiles->setText(QString("%1/%2").arg(numFilesProcessed).arg(totalNumFiles));
	setWindowTitle(_titleTemplate.arg(totalPercentage).arg(fileSizeToString(_speed)));
}

void CCopyMoveDialog::onProcessHalted(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage)
{
	CPromptDialog prompt (this, _op, reason, source, dest);
	if (!errorMessage.isEmpty())
		qDebug() << "halted because of error: " << errorMessage;

	ui->_overallProgress->setState(psStopped);
	const UserResponse response = prompt.ask();
	_performer->userResponse(reason, response, response == urRename ? prompt.newName() : QString());
	ui->_overallProgress->setState(_performer->paused() ? psPaused : psNormal);
}

void CCopyMoveDialog::onProcessFinished()
{
	delete _performer;
	_performer = 0;
	close();
}

void CCopyMoveDialog::onCurrentFileChanged(QString file)
{
	ui->_lblFileName->setText(file);
}

void CCopyMoveDialog::cancelPressed()
{
	if (QMessageBox::question(this, "Cancel?", "Are you sure you want to cancel this operation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		cancel();
}

void CCopyMoveDialog::pauseResume()
{
	ui->_btnPause->setText(_performer->togglePause() ? "Resume" : "Pause");
	ui->_overallProgress->setState(_performer->paused() ? psPaused : psNormal);
}

void CCopyMoveDialog::background()
{
	ui->_lblOperationName->setVisible(false);
	ui->_btnBackground->setVisible(false);
	ui->_fileProgress->setVisible(false);
	QTimer::singleShot(0, this, SLOT(setMinSize()));
}

void CCopyMoveDialog::setMinSize()
{
	setGeometry(QRect(geometry().topLeft(), QPoint(geometry().topLeft().x() + minimumSize().width(), geometry().topLeft().y() + minimumSize().height())));
	_mainWindow->raise();
	_mainWindow->activateWindow();
}

void CCopyMoveDialog::processEvents()
{
	std::lock_guard<std::mutex> lock(_callbackMutex);
	for (auto event = _callbacks.begin(); event != _callbacks.end(); ++event)
		(*event)();
	_callbacks.clear();
}

void CCopyMoveDialog::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close && _performer)
	{
		if (QMessageBox::question(this, "Abort?", "Do you want to abort the operation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
		{
			cancel();
			QWidget::closeEvent(e);
		}
	}

	if (e->type() == QCloseEvent::Close)
		emit closed();
}

void CCopyMoveDialog::cancel()
{
	_performer->cancel();
	ui->_btnCancel->setEnabled(false);
	ui->_btnPause->setEnabled(false);
}
