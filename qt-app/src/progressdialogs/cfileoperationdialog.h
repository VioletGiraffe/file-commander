#pragma once

#include "cfileoperationprompt.h" // PromptOperation
#include "fileoperations/cfileoperationjob.h"

DISABLE_COMPILER_WARNINGS
#include <QPoint>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <optional>

namespace Ui {
class CFileOperationDialog;
}

class QTimer;

// The one internal-operation dialog for copy, move, and permanent delete. Owns one CFileOperationJob,
// drains its event queue on a timer, and renders scanning / byte-or-item working progress / decision
// prompts / completion. Operation policy stays in the core; the dialog only formats what the job reports.
//
// It never depends on CMainWindow: the background-stacking anchor is injected as a provider, because the
// main window is a collaborator (not a base) and because the component tests construct the dialog without
// an application. Decision presentation is a protected virtual so a test can answer without a modal loop.
class CFileOperationDialog : public QWidget, private CFileOperationListener
{
public:
	// backgroundAnchorProvider returns the bottom-left corner the next backgrounded dialog should occupy;
	// empty is allowed (background mode then keeps the current position).
	CFileOperationDialog(FileOperationRequest request, std::function<QPoint()> backgroundAnchorProvider,
		QWidget* parent = nullptr, uint64_t transferChunkSize = 8 * 1024 * 1024);
	~CFileOperationDialog() override;

	CFileOperationDialog(const CFileOperationDialog&) = delete;
	CFileOperationDialog& operator=(const CFileOperationDialog&) = delete;

	// Starts the job and the event timer. Separate from construction so the caller can register and place
	// the dialog before any event is emitted.
	void start();

	// The completed operation's summary once finished; nullopt while still running. Presentation state,
	// also the component tests' assertion surface.
	[[nodiscard]] const std::optional<OperationSummary>& result() const noexcept { return _result; }

	// The human-readable completion text the dialog shows, composed from the summary's counters and
	// bounded diagnostics. Static and pure so it can be verified directly.
	[[nodiscard]] static QString composeSummaryText(const OperationSummary& summary, PromptOperation operation);

	// The cancellation action without the confirmation prompt: request cancellation (never joins) and
	// lock out further control. The Cancel button wraps this in a confirmation; tests call it directly.
	void requestCancellation();

	void setPaused(bool paused);

	[[nodiscard]] bool isInBackgroundMode() const noexcept { return _isInBackgroundMode; }

protected:
	// Presents one decision request and returns the user's choice. The production path shows a modal
	// CFileOperationPrompt; a test overrides this to answer synchronously.
	[[nodiscard]] virtual Decision presentDecision(const DecisionRequest& request);

	// Formats one snapshot into the widgets. Protected so the rendering branches can be verified with
	// synthetic snapshots, without racing a live job.
	void renderProgress(const ProgressSnapshot& snapshot);

	// Drains and dispatches the queued job events. Timer-driven in production; protected so a test can stop
	// the timer and single-step the drain to exercise the decision-suppression guard deterministically.
	void drainEvents();

	void closeEvent(QCloseEvent* e) override;

private:
	void onOperationEvent(const OperationEvent& event) override;

	void handleDecisionRequest(const DecisionRequest& request);
	void handleCompletion(const OperationSummary& summary);

	void togglePause();
	void switchToBackground();
	[[nodiscard]] bool confirmCancellation();

	Ui::CFileOperationDialog* ui;

	// Declared before _job: their initializers read the request that _job's initializer then moves from.
	const PromptOperation _operation;
	const PrimaryProgressUnit _primaryUnit;
	const std::function<QPoint()> _backgroundAnchorProvider;

	CFileOperationJob _job;

	QTimer* _eventTimer = nullptr;
	bool _draining = false; // A modal prompt spins a nested loop; the timer must not re-enter the drain
	bool _paused = false;
	bool _isInBackgroundMode = false;
	std::optional<OperationSummary> _result;
};
