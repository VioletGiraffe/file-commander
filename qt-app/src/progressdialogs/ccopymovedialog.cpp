#include "ccopymovedialog.h"
#include "../cmainwindow.h"
#include "cpromptdialog.h"
#include "filesystemhelperfunctions.h"
#include "progressdialoghelpers.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "math/math.hpp"

DISABLE_COMPILER_WARNINGS
#include "ui_ccopymovedialog.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CCopyMoveDialog::CCopyMoveDialog(QWidget* parent, Operation operation, std::vector<CFileSystemObject>&& source, QString destination, CMainWindow * mainWindow) :
	QWidget(parent, Qt::Window),
	ui(new Ui::CCopyMoveDialog),
	_performer(new COperationPerformer(operation, std::move(source), std::move(destination))),
	_mainWindow(mainWindow),
	_op(operation),
	_titleTemplate(_op == operationCopy ? tr("%1% Copying %2/s, %3 remaining") : tr("%1% Moving %2/s, %3 remaining")),
	_labelTemplate(_op == operationCopy ? tr("Copying files... %2/s, %3 remaining") : tr("Moving files... %2/s, %3 remaining"))
{
	ui->setupUi(this);
	ui->_overallProgress->linkToWidgetstaskbarButton(this);

	ui->_overallProgressText->setMinimumWidth(QFontMetrics(ui->_overallProgressText->font()).boundingRect(QSL("100.0%")).width());
	ui->_fileProgressText->setMinimumWidth(QFontMetrics(ui->_fileProgressText->font()).boundingRect(QSL("100.0%")).width());

	ui->_lblFileName->clear();

	assert_r(mainWindow);

	if (operation == operationCopy)
		ui->_lblOperationName->setText(tr("Copying files..."));
	else if (operation == operationMove)
		ui->_lblOperationName->setText(tr("Moving files..."));
	else
		assert_unconditional_r("Unknown operation");

	connect (ui->_btnCancel,     &QPushButton::clicked, this, &CCopyMoveDialog::cancelPressed);
	connect (ui->_btnBackground, &QPushButton::clicked, this, &CCopyMoveDialog::switchToBackground);
	connect (ui->_btnPause,      &QPushButton::clicked, this, &CCopyMoveDialog::pauseResume);

	setWindowTitle(ui->_lblOperationName->text());

	_eventsProcessTimer = new QTimer{ this };
	_eventsProcessTimer->setInterval(250);
	_eventsProcessTimer->start();
	connect(_eventsProcessTimer, &QTimer::timeout, this, [this]() {processEvents();});

	_performer->setObserver(this);
	_performer->start();
}

CCopyMoveDialog::~CCopyMoveDialog()
{
	if (_performer)
		_performer->cancel();

	delete ui;
}

void CCopyMoveDialog::onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed, uint32_t secondsRemaining)
{
	ui->_overallProgress->setValue(Math::round<int>(totalPercentage));
	ui->_overallProgressText->setText(QString::number(totalPercentage, 'f', 1).append('%'));

	ui->_fileProgress->setValue(Math::round<int>(filePercentage));
	ui->_fileProgressText->setText(QString::number(filePercentage, 'f', 1).append('%'));


	ui->_lblOperationName->setText(_labelTemplate.arg(fileSizeToString(speed), secondsToTimeIntervalString(secondsRemaining)));
	ui->_lblNumFiles->setText(QSL("%1/%2").arg(numFilesProcessed).arg(totalNumFiles));
	setWindowTitle(_titleTemplate.arg(QString::number(totalPercentage, 'f', 1), fileSizeToString(speed), secondsToTimeIntervalString(secondsRemaining)));
}

void CCopyMoveDialog::onProcessHalted(HaltReason reason, const CFileSystemObject& source, const CFileSystemObject& dest, const QString& errorMessage)
{
	CPromptDialog prompt(this, _op, reason, source, dest, errorMessage);

	ui->_overallProgress->setState(psStopped);
	const UserResponse response = prompt.ask();
	_performer->userResponse(reason, response, response == urRename ? prompt.newName() : QString());
	ui->_overallProgress->setState(_performer->paused() ? psPaused : psNormal);
}

void CCopyMoveDialog::onProcessFinished(const QString& message)
{
	_performer.reset();
	close();

	if (!message.isEmpty())
		QMessageBox::information(this, tr("Operation finished"), message);
}

void CCopyMoveDialog::onCurrentFileChanged(const QString& file)
{
	ui->_lblFileName->setText(file);
}

// True if cancelled, false if the user chose to continue
bool CCopyMoveDialog::cancelPressed()
{
	const bool working = _performer && _performer->working();
	if (!working)
		return true;

	const bool wasPaused = _performer->paused();
	if (!wasPaused)
		pauseResume();

	if (QMessageBox::question(this, tr("Cancel?"), tr("Are you sure you want to cancel this operation?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		pauseResume();
		cancel();
		return true;
	}
	else if (!wasPaused)
		pauseResume();

	return false;
}

void CCopyMoveDialog::pauseResume()
{
	ui->_btnPause->setText(_performer->togglePause() ? tr("Resume") : tr("Pause"));
	ui->_overallProgress->setState(_performer->paused() ? psPaused : psNormal);
}

void CCopyMoveDialog::switchToBackground()
{
	ui->_lblOperationName->hide();
	ui->_btnBackground->hide();
	ui->_fileProgress->hide();
	ui->_fileProgressText->hide();
	QTimer::singleShot(0, this, [this](){
		const QSize minsize = minimumSize();
		const QPoint mainWindowTopLeft = _mainWindow->geometry().topLeft();
		const QRect newGeometry = QRect(QPoint(mainWindowTopLeft.x(), mainWindowTopLeft.y() - minsize.height()), minsize);
		setGeometry(newGeometry);

		_mainWindow->activateWindow();
		raise();
	});
}

void CCopyMoveDialog::updateUiWthProgress()
{
}

void CCopyMoveDialog::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close && _performer)
	{
		if (cancelPressed())
		{
			QWidget::closeEvent(e);
			emit closed();
			return;
		}
	}
	else if (e->type() == QCloseEvent::Close)
	{
		QWidget::closeEvent(e);
		emit closed();
		return;
	}

	if (e)
		e->ignore();
}

void CCopyMoveDialog::cancel()
{
	_performer->cancel();
	ui->_btnCancel->setEnabled(false);
	ui->_btnPause->setEnabled(false);
}
