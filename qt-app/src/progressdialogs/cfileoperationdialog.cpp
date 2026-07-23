#include "cfileoperationdialog.h"
#include "cfileoperationprompt.h"
#include "filesystemhelperfunctions.h"
#include "progressdialoghelpers.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()
#include "math/math.hpp"

#include "qtcore_helpers/qstring_helpers.hpp" // QSL

DISABLE_COMPILER_WARNINGS
#include "ui_cfileoperationdialog.h"

#include <QCloseEvent>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

namespace
{

PromptOperation operationFromRequest(const FileOperationRequest& request)
{
	if (const auto* transfer = std::get_if<TransferRequest>(&request))
		return transfer->kind == TransferKind::Move ? PromptOperation::Move : PromptOperation::Copy;
	return PromptOperation::Delete;
}

PrimaryProgressUnit unitFromRequest(const FileOperationRequest& request)
{
	return std::holds_alternative<PermanentDeleteRequest>(request) ? PrimaryProgressUnit::Items : PrimaryProgressUnit::Bytes;
}

QString operationVerb(const PromptOperation operation)
{
	switch (operation)
	{
	case PromptOperation::Copy: return QObject::tr("Copying");
	case PromptOperation::Move: return QObject::tr("Moving");
	case PromptOperation::Delete: return QObject::tr("Deleting");
	}
	return {};
}

// The primary-unit rate, formatted for its unit: bytes per second, or whole items per second.
QString speedText(const uint64_t primaryUnitsPerSecond, const PrimaryProgressUnit unit)
{
	if (unit == PrimaryProgressUnit::Bytes)
		return CFileOperationDialog::tr("%1/s").arg(fileSizeToString(primaryUnitsPerSecond));
	return CFileOperationDialog::tr("%1 items/s").arg(primaryUnitsPerSecond);
}

} // namespace

CFileOperationDialog::CFileOperationDialog(FileOperationRequest request, std::function<QPoint()> backgroundAnchorProvider,
	QWidget* parent, const uint64_t transferChunkSize) :
	QWidget(parent, Qt::Window),
	ui(new Ui::CFileOperationDialog),
	_operation(operationFromRequest(request)),
	_primaryUnit(unitFromRequest(request)),
	_backgroundAnchorProvider(mv(backgroundAnchorProvider)),
	_job(mv(request), transferChunkSize)
{
	ui->setupUi(this);
	ui->_overallProgress->linkToWidgetstaskbarButton(this);

	ui->_lblCurrentFile->clear();
	ui->_lblStatus->clear();
	ui->_lblSummary->hide();
	setWindowTitle(operationVerb(_operation) % QSL("..."));
	ui->_lblOperationName->setText(windowTitle());

	// The per-file byte bar is meaningful only for byte-unit transfers; delete tracks whole items.
	if (_primaryUnit != PrimaryProgressUnit::Bytes)
	{
		ui->_fileProgress->hide();
		ui->_fileProgressText->hide();
	}

	// While running this button cancels (and the dialog lives on until the job reports); once finished it
	// is relabeled Close and simply dismisses the dialog.
	connect(ui->_btnCancel, &QPushButton::clicked, this, [this] {
		if (_result)
			close();
		else
			(void)confirmCancellation();
	});
	connect(ui->_btnPause, &QPushButton::clicked, this, &CFileOperationDialog::togglePause);
	connect(ui->_btnBackground, &QPushButton::clicked, this, &CFileOperationDialog::switchToBackground);

	_eventTimer = new QTimer{ this };
	_eventTimer->setObjectName(QSL("eventTimer")); // Named so a test can find and stop it unambiguously
	_eventTimer->setInterval(100);
	connect(_eventTimer, &QTimer::timeout, this, &CFileOperationDialog::drainEvents);

	adjustSize();
}

void CFileOperationDialog::start()
{
	assert_debug_only(_job.status() == JobStatus::NotStarted);
	_job.start();
	_eventTimer->start();
}

CFileOperationDialog::~CFileOperationDialog()
{
	// The job destructor requests cancellation and joins; nothing here may outlive that.
	delete ui;
}

void CFileOperationDialog::drainEvents()
{
	if (_draining)
		return; // A modal prompt is spinning a nested event loop; do not re-enter the drain

	_draining = true;
	_job.processEvents(*this);
	_draining = false;
}

void CFileOperationDialog::onOperationEvent(const OperationEvent& event)
{
	std::visit([this]<typename E>(const E& payload) {
		if constexpr (std::is_same_v<E, ProgressSnapshot>)
			renderProgress(payload);
		else if constexpr (std::is_same_v<E, DecisionRequest>)
			handleDecisionRequest(payload);
		else
			handleCompletion(payload);
	}, event);
}

void CFileOperationDialog::renderProgress(const ProgressSnapshot& snapshot)
{
	if (snapshot.currentEntry)
		ui->_lblCurrentFile->setText(QDir::toNativeSeparators(snapshot.currentEntry->value()));

	if (snapshot.phase == OperationPhase::Scanning)
	{
		// Indeterminate overall bar plus the running discovered count; no total, no ETA yet.
		ui->_overallProgress->setMaximum(0);
		ui->_overallProgressText->clear();
		ui->_lblStatus->setText(tr("Scanning... %1 items found").arg(snapshot.itemsProcessed));
		ui->_lblOperationName->setText(tr("Scanning..."));
		return;
	}

	const bool bytes = _primaryUnit == PrimaryProgressUnit::Bytes;
	const uint64_t processed = bytes ? snapshot.bytesProcessed : snapshot.itemsProcessed;
	const std::optional<uint64_t> total = bytes ? snapshot.bytesTotal
		: (snapshot.itemsTotal ? std::optional<uint64_t>{ *snapshot.itemsTotal } : std::nullopt);

	if (total && *total > 0)
	{
		ui->_overallProgress->setMaximum(100);
		const int percent = Math::round<int>(static_cast<double>(processed) * 100.0 / static_cast<double>(*total));
		ui->_overallProgress->setValue(percent);
		ui->_overallProgressText->setText(QString::number(percent) % QLatin1Char('%'));
	}
	else
	{
		ui->_overallProgress->setMaximum(0); // Totals not yet known (a manifest is still pending)
		ui->_overallProgressText->clear();
	}

	if (bytes && snapshot.currentEntryBytesTotal && *snapshot.currentEntryBytesTotal > 0)
	{
		const int filePercent = Math::round<int>(static_cast<double>(snapshot.currentEntryBytesProcessed) * 100.0 / static_cast<double>(*snapshot.currentEntryBytesTotal));
		ui->_fileProgress->setValue(filePercent);
		ui->_fileProgressText->setText(QString::number(filePercent) % QLatin1Char('%'));
	}

	QString status = operationVerb(_operation) % QSL("  ") % speedText(snapshot.primaryUnitsPerSecond, _primaryUnit);
	if (snapshot.secondsRemaining)
		status += QSL(", ") % secondsToTimeIntervalString(*snapshot.secondsRemaining) % QSL(" remaining");
	ui->_lblStatus->setText(status);
	ui->_lblOperationName->setText(operationVerb(_operation) % QSL("..."));
}

void CFileOperationDialog::handleDecisionRequest(const DecisionRequest& request)
{
	// A cancellation that landed after this event left the queue invalidates the prompt: do not present it.
	if (!_job.hasPendingDecision())
		return;

	ui->_overallProgress->setState(psStopped);
	const Decision decision = presentDecision(request);
	_job.submitDecision(decision); // false only if cancellation already won; nothing to do then
	ui->_overallProgress->setState(_paused ? psPaused : psNormal);
}

void CFileOperationDialog::handleCompletion(const OperationSummary& summary)
{
	_result = summary;
	_eventTimer->stop();

	ui->_overallProgress->setState(summary.status == CompletionStatus::Failed ? psStopped : psNormal);
	ui->_fileProgress->hide();
	ui->_fileProgressText->hide();
	ui->_lblCurrentFile->clear();
	ui->_lblStatus->clear();
	ui->_lblOperationName->hide();
	ui->_btnPause->hide();
	ui->_btnBackground->hide();
	ui->_btnCancel->setText(tr("Close"));
	ui->_btnCancel->setEnabled(true);

	ui->_lblSummary->setText(composeSummaryText(summary, _operation));
	ui->_lblSummary->show();
}

QString CFileOperationDialog::composeSummaryText(const OperationSummary& summary, const PromptOperation operation)
{
	const QString verb = operation == PromptOperation::Delete ? tr("deleted")
		: operation == PromptOperation::Move ? tr("moved") : tr("copied");

	QString headline;
	switch (summary.status)
	{
	case CompletionStatus::Completed: headline = tr("Operation finished."); break;
	case CompletionStatus::Cancelled: headline = tr("Operation cancelled."); break;
	case CompletionStatus::Failed: headline = tr("Operation failed."); break;
	}

	// A clean run where nothing actually had to be done - including one where every item was already
	// satisfied (the desired end state already held). Already-satisfied alone does not count as activity.
	const bool nothingHappened = summary.completedItems == 0 && summary.skippedItems == 0
		&& summary.failedItems == 0 && summary.warningCount == 0;
	if (nothingHappened && summary.status == CompletionStatus::Completed)
		return tr("Nothing needed to be %1.").arg(verb);

	QStringList facts;
	if (summary.completedItems > 0)
		facts += tr("%1 items %2").arg(summary.completedItems).arg(verb);
	if (summary.skippedItems > 0)
		facts += tr("%1 skipped").arg(summary.skippedItems);
	if (summary.failedItems > 0)
		facts += tr("%1 failed").arg(summary.failedItems);
	if (summary.alreadySatisfiedItems > 0)
		facts += tr("%1 already up to date").arg(summary.alreadySatisfiedItems);
	if (summary.warningCount > 0)
		facts += tr("%1 warnings").arg(summary.warningCount);

	QString text = facts.isEmpty() ? headline : headline % QSL("\n") % facts.join(QSL(", "));

	// The bounded representative diagnostics: failures carry the incomplete work, warnings the recoverable
	// conditions. Show failures first; they are what the user must act on.
	for (const OperationDiagnostic& failure : summary.representativeFailures)
		text += QSL("\n") % fileOperationDiagnosticText(failure);
	for (const OperationDiagnostic& warning : summary.representativeWarnings)
		text += QSL("\n") % fileOperationDiagnosticText(warning);

	return text;
}

Decision CFileOperationDialog::presentDecision(const DecisionRequest& request)
{
	CFileOperationPrompt prompt{ request, _operation, this };
	return prompt.ask();
}

void CFileOperationDialog::requestCancellation()
{
	_job.cancel();
	ui->_btnCancel->setEnabled(false);
	ui->_btnPause->setEnabled(false);
}

void CFileOperationDialog::setPaused(const bool paused)
{
	if (_paused == paused)
		return;

	_paused = paused;
	_job.setPaused(paused);
	ui->_btnPause->setText(_paused ? tr("Resume") : tr("Pause"));
	ui->_overallProgress->setState(_paused ? psPaused : psNormal);
}

void CFileOperationDialog::togglePause()
{
	setPaused(!_paused);
}

void CFileOperationDialog::switchToBackground()
{
	ui->_lblCurrentFile->hide();
	ui->_fileProgress->hide();
	ui->_fileProgressText->hide();
	ui->_btnBackground->hide();

	if (!_backgroundAnchorProvider)
	{
		_isInBackgroundMode = true;
		return;
	}

	// Queued: hiding the detail widgets only posts a deferred LayoutRequest, so an adjustSize() in this
	// stack would measure the not-yet-relaid-out size.
	QMetaObject::invokeMethod(this, [this] {
		adjustSize(); // Shrink to fit now that the per-file detail widgets are hidden
		const QPoint anchor = _backgroundAnchorProvider(); // Bottom-left corner
		move(anchor.x(), anchor.y() - height());
		_isInBackgroundMode = true;
	}, Qt::QueuedConnection);
}

bool CFileOperationDialog::confirmCancellation()
{
	if (_result) // Already finished: the button now means Close
		return true;

	const bool wasPaused = _paused;
	if (!wasPaused)
		setPaused(true);

	// This modal is not reached through drainEvents, so guard the drain explicitly: a decision prompt
	// must not stack on top of the confirmation while the worker keeps reporting.
	_draining = true;
	const auto answer = QMessageBox::question(this, tr("Cancel?"), tr("Are you sure you want to cancel this operation?"),
		QMessageBox::Yes | QMessageBox::No);
	_draining = false;

	if (answer == QMessageBox::Yes)
	{
		requestCancellation();
		return true;
	}

	if (!wasPaused && !_result)
		setPaused(false);
	return false;
}

void CFileOperationDialog::closeEvent(QCloseEvent* e)
{
	// A finished operation closes freely; a running one asks first and stays alive until it truly ends.
	if (_result || confirmCancellation())
		QWidget::closeEvent(e);
	else
		e->ignore();
}
