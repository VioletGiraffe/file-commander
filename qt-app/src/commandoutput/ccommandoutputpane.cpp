#include "ccommandoutputpane.h"

#include "widgets/clabelelided.h"

DISABLE_COMPILER_WARNINGS
#include <QEnterEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

namespace {

constexpr int AUTO_CLOSE_SECONDS = 10;
constexpr int MAX_OUTPUT_LINES = 5000;

}

CCommandOutputPane::CCommandOutputPane(QWidget* parent) :
	QWidget(parent),
	_commandLabel{ new CLabelElided(this) },
	_statusLabel{ new QLabel(this) },
	_stopButton{ new QToolButton(this) },
	_pinButton{ new QToolButton(this) },
	_closeButton{ new QToolButton(this) },
	_output{ new QPlainTextEdit(this) }
{
	_commandLabel->setElideMode(Qt::ElideRight);

	_stopButton->setText(QStringLiteral("■"));
	_stopButton->setFocusPolicy(Qt::NoFocus);

	_pinButton->setCheckable(true);
	_pinButton->setText(QStringLiteral("📌"));
	_pinButton->setToolTip(tr("Keep this output open"));
	_pinButton->setFocusPolicy(Qt::NoFocus);

	_closeButton->setText(QStringLiteral("✕"));
	_closeButton->setToolTip(tr("Close"));
	_closeButton->setFocusPolicy(Qt::NoFocus);

	// Console output keeps its column layout: wrapping would break tables and aligned listings
	_output->setLineWrapMode(QPlainTextEdit::NoWrap);
	_output->setReadOnly(true);
	_output->setFocusPolicy(Qt::ClickFocus);
	_output->setMaximumBlockCount(MAX_OUTPUT_LINES);
	_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

	auto* header = new QHBoxLayout;
	header->setContentsMargins(0, 0, 0, 0);
	header->addWidget(_commandLabel, 1);
	header->addWidget(_statusLabel);
	header->addWidget(_stopButton);
	header->addWidget(_pinButton);
	header->addWidget(_closeButton);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	layout->addLayout(header);
	layout->addWidget(_output);

	_countdownTimer.setInterval(1000);
	connect(&_countdownTimer, &QTimer::timeout, this, [this] {
		if (--_secondsLeft > 0)
		{
			showCountdown();
			return;
		}

		_countdownTimer.stop();
		if (onCloseRequested)
			onCloseRequested();
	});

	connect(_stopButton, &QToolButton::clicked, this, [this] {
		const bool force = _stopRequested;
		_stopRequested = true;
		_statusLabel->setText(tr("Stopping"));
		_stopButton->setToolTip(tr("Force stop"));
		if (onStopRequested)
			onStopRequested(force);
	});
	connect(_pinButton, &QToolButton::toggled, this, &CCommandOutputPane::updateCountdown);
	connect(_closeButton, &QToolButton::clicked, this, [this] {
		if (onCloseRequested)
			onCloseRequested();
	});

	QScrollBar* verticalScrollBar = _output->verticalScrollBar();
	// The range changes with new output and with the viewport height
	connect(verticalScrollBar, &QScrollBar::rangeChanged, this, [this, verticalScrollBar](int /*min*/, int max) {
		if (_followTail)
			verticalScrollBar->setValue(max);
	});
	connect(verticalScrollBar, &QScrollBar::valueChanged, this, [this, verticalScrollBar](int value) {
		_followTail = value == verticalScrollBar->maximum();
	});

	// actionTriggered comes from the user only, never from tail-following
	connect(verticalScrollBar, &QScrollBar::actionTriggered, this, &CCommandOutputPane::markUserInteraction);
	connect(_output->horizontalScrollBar(), &QScrollBar::actionTriggered, this, &CCommandOutputPane::markUserInteraction);
	connect(_output, &QPlainTextEdit::selectionChanged, this, [this] {
		if (_output->textCursor().hasSelection())
			markUserInteraction();
	});
}

void CCommandOutputPane::attach(const QString& command)
{
	_finished = false;
	_succeeded = false;
	_userInteracted = false;
	_stopRequested = false;
	_finishedStatus.clear();

	_commandLabel->setText(command);
	_statusLabel->setText(tr("Running"));
	_stopButton->setToolTip(tr("Stop"));
	_stopButton->show();
	_followTail = true;
	_output->clear();
	_pinButton->setChecked(false);
	_closeButton->setEnabled(false);
	updateCountdown();
}

void CCommandOutputPane::appendOutput(const QString& text)
{
	QTextCursor cursor{ _output->document() };
	cursor.movePosition(QTextCursor::End);
	// A carriage return has no glyph: a CRLF becomes a line break, and lines a progress meter rewrote with CR run together
	cursor.insertText(QString{ text }.remove('\r'));
}

void CCommandOutputPane::markFinished(int exitCode, bool normalExit)
{
	if (_stopRequested)
		setFinishedStatus(tr("Stopped"), true);
	else if (!normalExit)
		setFinishedStatus(tr("Crashed"), false);
	else if (exitCode != 0)
		setFinishedStatus(tr("Exit code %1").arg(exitCode), false);
	else
		setFinishedStatus(tr("Done"), true);
}

void CCommandOutputPane::markFailedToStart()
{
	setFinishedStatus(tr("Failed to start"), false);
}

bool CCommandOutputPane::isReusable() const
{
	return _finished && _succeeded && !_userInteracted && !_hovered && !_pinButton->isChecked();
}

void CCommandOutputPane::enterEvent(QEnterEvent* event)
{
	QWidget::enterEvent(event);
	_hovered = true;
	updateCountdown();
}

void CCommandOutputPane::leaveEvent(QEvent* event)
{
	QWidget::leaveEvent(event);
	_hovered = false;
	updateCountdown();
}

void CCommandOutputPane::setFinishedStatus(const QString& status, bool succeeded)
{
	_finished = true;
	_succeeded = succeeded;
	_finishedStatus = status;
	_stopButton->hide();
	_closeButton->setEnabled(true);
	updateCountdown();
}

void CCommandOutputPane::markUserInteraction()
{
	_userInteracted = true;
	updateCountdown();
}

void CCommandOutputPane::updateCountdown()
{
	if (!isReusable())
	{
		_countdownTimer.stop();
		if (_finished)
			_statusLabel->setText(_finishedStatus);
		return;
	}

	if (_countdownTimer.isActive())
		return;

	_secondsLeft = AUTO_CLOSE_SECONDS;
	showCountdown();
	_countdownTimer.start();
}

void CCommandOutputPane::showCountdown()
{
	_statusLabel->setText(tr("%1, closing in %2 s").arg(_finishedStatus).arg(_secondsLeft));
}
