#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QTimer>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <functional>

class CLabelElided;
class QEnterEvent;
class QLabel;
class QPlainTextEdit;
class QToolButton;

// One command's output under a header with the command, its status, and pin and close buttons.
// A pane is reusable exactly when it may close on its own: finished, succeeded, unpinned, and not interacted with.
// Hovering pauses the countdown; a selection or a scroll cancels it until another command is attached.
class CCommandOutputPane final : public QWidget
{
public:
	explicit CCommandOutputPane(QWidget* parent);

	// When the countdown ends or the close button is clicked, which it can be only once the command has finished
	std::function<void()> onCloseRequested;

	// Clears the pane for `command`, which is running
	void attach(const QString& command);
	void appendOutput(const QString& text);
	void markFinished(int exitCode, bool normalExit);
	void markFailedToStart();

	[[nodiscard]] bool isReusable() const;

protected:
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void setFinishedStatus(const QString& status, bool succeeded);
	void markUserInteraction();
	// Restarts the full countdown whenever the pane becomes reusable, and stops it when it no longer is
	void updateCountdown();
	void showCountdown();

	CLabelElided* _commandLabel;
	QLabel* _statusLabel;
	QToolButton* _pinButton;
	QToolButton* _closeButton;
	QPlainTextEdit* _output;

	QTimer _countdownTimer;
	int _secondsLeft = 0;

	QString _finishedStatus;
	bool _finished = false;
	bool _succeeded = false;
	bool _userInteracted = false; // A selection or a scroll since attach()
	bool _hovered = false;
	bool _followTail = true; // The view is at the last line and stays there as the range changes
};
