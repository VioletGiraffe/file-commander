#include "ctuifileoperations.h"
#include "ctuidialogs.h"

#include "ccontroller.h"
#include "fileoperationlaunch.h"
#include "filesystemhelperfunctions.h"

#include "fileoperations/cfileoperationjob.h"
#include "fileoperations/fileoperationtexts.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TObject
#define Uses_TProgram
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <3rdparty/magic_enum/magic_enum.hpp>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace
{

constexpr ushort cmPauseOperation = 1900;

[[nodiscard]] Decision askForDecision(const DecisionRequest& request, const PromptOperation operation)
{
	const OperationIssue& issue = request.issue;
	const QString sourceName = issue.source.path.name();
	const auto indexOf = [&request](const DecisionAction action) -> std::optional<size_t> {
		const auto it = std::ranges::find(request.allowedActions, action);
		return it != request.allowedActions.end() ? std::optional<size_t>{ (size_t)(it - request.allowedActions.begin()) } : std::nullopt;
	};

	DialogSpec prompt;
	prompt.title = fileOperationName(operation);
	prompt.lines.push_back({ fileOperationIssueText(issue, operation) });
	prompt.lines.push_back({});
	prompt.lines.push_back({ QStringLiteral("Source: ") + toNativeSeparators(issue.source.path.value()), true });
	prompt.lines.push_back({ QStringLiteral("  ") + fileOperationEntryDetailsText(issue.source) });
	if (issue.destination)
	{
		prompt.lines.push_back({ QStringLiteral("Destination: ") + toNativeSeparators(issue.destination->path.value()), true });
		prompt.lines.push_back({ QStringLiteral("  ") + fileOperationEntryDetailsText(*issue.destination) });
	}
	for (const QString& text : { fileOperationIssueFailureText(issue), fileOperationCancelConsequencesText(request) })
	{
		if (text.isEmpty())
			continue;

		prompt.lines.push_back({});
		for (const QString& line : text.split(u'\n'))
			prompt.lines.push_back({ line });
	}

	for (const DecisionAction action : request.allowedActions)
		prompt.buttons.push_back(fileOperationActionLabel(action, operation));
	const std::optional<DecisionAction> defaultAction = defaultActionFor(issue.kind);
	const std::optional<size_t> defaultButton = defaultAction ? indexOf(*defaultAction) : std::nullopt;
	prompt.defaultButton = defaultButton;
	// Turbo Vision gives Enter to the focused button, so a kind without a default action starts on Skip, which changes nothing
	prompt.focusedButton = defaultButton ? defaultButton : indexOf(DecisionAction::Skip);

	const std::optional<size_t> renameButton = indexOf(DecisionAction::Rename);
	if (renameButton)
	{
		prompt.lines.push_back({});
		prompt.lines.push_back({ QStringLiteral("New name, for Rename:") });
		prompt.inputText = sourceName;
		prompt.focusedButton.reset(); // The name field takes the focus
		// A changed name is a rename, and Rename explains a name it cannot use; the unchanged name keeps the kind's default
		prompt.enterInInput = [renameButton, defaultButton, &sourceName](const QString& newName) {
			return newName != sourceName ? renameButton : defaultButton;
		};
		prompt.accepts = [renameButton, &sourceName](const size_t button, const QString& newName) {
			if (button != *renameButton)
				return true;

			const auto refusal = renameRefusal(newName, sourceName);
			if (refusal)
				showUserMessage(*refusal);
			return !refusal;
		};
	}
	if (remainingMatchingScopeOffered(request))
		prompt.checkBoxLabel = fileOperationScopeLabel(issue.kind);

	const DialogAnswer answer = execDialog(prompt);
	if (!answer.button)
		return Decision{ DecisionAction::Cancel };

	Decision decision{ request.allowedActions[*answer.button] };
	if (answer.checked && isActionRememberable(issue.kind, decision.action))
		decision.scope = DecisionScope::RemainingMatchingIssues;
	if (decision.action == DecisionAction::Rename)
		decision.newName = answer.inputText;
	return decision;
}

// nullopt cancels the operation. A row no button answers stays unanswered: the operation asks about it when it gets there.
[[nodiscard]] std::optional<ConflictResolutions> askForPlanAnswers(const ConflictPlan& plan, const PromptOperation operation)
{
	std::array<size_t, magic_enum::enum_count<SideComparison>()> comparisonCounts{};
	std::array<size_t, magic_enum::enum_count<IssueKind>()> kindCounts{};
	for (const PlannedConflict& conflict : plan)
	{
		++kindCounts[(size_t)conflict.issue.kind];
		if (conflict.issue.kind == IssueKind::FileReplacement)
			++comparisonCounts[(size_t)compareSides(conflict)];
	}
	const bool anyReplacement = kindCounts[(size_t)IssueKind::FileReplacement] > 0;
	const size_t mergeCount = kindCounts[(size_t)IssueKind::RootDirectoryMerge];

	ConflictResolutions resolutions(plan.size());
	if (!anyReplacement && mergeCount == 0)
		return resolutions;

	DialogSpec prompt;
	prompt.title = QStringLiteral("%1: conflicts").arg(fileOperationName(operation));
	const auto addCount = [&prompt](const size_t count, const QString& text) {
		if (count > 0)
			prompt.lines.push_back({ QStringLiteral("%1  %2").arg(count, 6).arg(text) });
	};
	for (const SideComparison comparison : magic_enum::enum_values<SideComparison>())
		addCount(comparisonCounts[(size_t)comparison], sideComparisonText(comparison));
	addCount(mergeCount, fileOperationIssueKindText(IssueKind::RootDirectoryMerge));

	bool headingAdded = false;
	for (const IssueKind kind : magic_enum::enum_values<IssueKind>())
	{
		if (kind == IssueKind::FileReplacement || kind == IssueKind::RootDirectoryMerge || kindCounts[(size_t)kind] == 0)
			continue;

		if (!headingAdded)
		{
			prompt.lines.push_back({});
			prompt.lines.push_back({ QStringLiteral("Asked one by one during the operation:") });
			headingAdded = true;
		}
		addCount(kindCounts[(size_t)kind], fileOperationIssueKindText(kind));
	}

	std::vector<PlanWideAnswer> answers;
	if (anyReplacement)
	{
		prompt.buttons = { QStringLiteral("Replace all"), QStringLiteral("Replace older"), QStringLiteral("Skip all") };
		answers = { PlanWideAnswer::ReplaceAll, PlanWideAnswer::ReplaceOlder, PlanWideAnswer::SkipAll };
	}
	else
	{
		// Every answer merges the folders; they differ only in the file replacements, and there are none
		prompt.buttons = { fileOperationActionLabel(DecisionAction::Merge, operation) };
		answers = { PlanWideAnswer::ReplaceAll };
	}
	prompt.buttons.push_back(QStringLiteral("Cancel"));
	prompt.defaultButton = prompt.focusedButton = 0;

	const DialogAnswer answer = execDialog(prompt);
	if (!answer.button || *answer.button >= answers.size())
		return {};

	for (size_t i = 0; i < plan.size(); ++i)
	{
		if (const auto action = planWideActionFor(plan[i], answers[*answer.button]))
			resolutions[i] = Decision{ *action };
	}
	return resolutions;
}

[[nodiscard]] QString progressBar(const std::optional<int> percent, const int width)
{
	const int barWidth = std::max(width - 5, 0);
	const int filled = percent ? barWidth * std::clamp(*percent, 0, 100) / 100 : 0;
	QString bar = QString(filled, u'█') + QString(barWidth - filled, u'░');
	if (percent)
		bar += QStringLiteral("%1%").arg(*percent, 4);
	return bar;
}

// Runs one operation and shows its progress. Not shown at once: see runOperation.
class CTuiProgressDialog final : public TDialog, private CFileOperationListener
{
public:
	explicit CTuiProgressDialog(FileOperationRequest request) :
		TWindowInit(&TDialog::initFrame),
		TDialog({ 0, 0, dialogWidth(), 11 }, stringView(titleFor(request).toUtf8())),
		_operation{ promptOperationFor(request) },
		_unit{ primaryProgressUnitFor(request) },
		_job{ std::move(request) }
	{
		options |= ofCentered;

		_text = new CTuiTextView({ 3, 2, size.x - 3, 7 }, {});
		insert(_text);

		const int middle = size.x / 2;
		_pauseButton = new TButton({ middle - 13, 8, middle - 1, 10 }, "~P~ause", cmPauseOperation, bfNormal);
		insert(_pauseButton);
		_cancelButton = new TButton({ middle + 1, 8, middle + 13, 10 }, "~C~ancel", cmCancel, bfNormal);
		insert(_cancelButton);
		_pauseButton->select();
	}

	void start() { _job.start(); }

	// Shows the job's queued progress and opens a dialog for each of its questions
	void processJobEvents()
	{
		// A question's dialog runs on the desktop, whose event loop does not pass through this dialog's getEvent
		assert_and_return_r(!_processingJobEvents, );
		_processingJobEvents = true;
		_job.processEvents(*this);
		_processingJobEvents = false;
	}

	[[nodiscard]] const std::optional<OperationSummary>& summary() const noexcept { return _summary; }
	[[nodiscard]] bool questionAsked() const noexcept { return _questionAsked; }

	void getEvent(TEvent& event) override
	{
		processJobEvents();
		if (_summary && (state & sfModal))
			endModal(cmOK);
		// Always fetches: a button tracking the mouse loops on this call until the mouse button is released
		TDialog::getEvent(event);
	}

	void handleEvent(TEvent& event) override
	{
		// Before the base class, which closes a modal dialog on cmCancel. Esc and the frame's close icon arrive as cmCancel too.
		if (event.what == evCommand && event.message.command == cmCancel)
		{
			confirmCancellation();
			clearEvent(event);
			return;
		}

		TDialog::handleEvent(event);
		if (event.what == evCommand && event.message.command == cmPauseOperation)
		{
			setPaused(!_paused);
			clearEvent(event);
		}
	}

private:
	[[nodiscard]] static int dialogWidth() { return std::min(72, TProgram::deskTop->size.x - 2); }

	[[nodiscard]] static QString titleFor(const FileOperationRequest& request)
	{
		const QString route = fileOperationRouteText(request);
		const QString verb = fileOperationProgressVerb(promptOperationFor(request));
		// The frame cuts a title off at its width less 10 cells, and a space on each side keeps it off the frame
		return elidedInTheMiddle(route.isEmpty() ? verb : verb + u' ' + route, dialogWidth() - 12);
	}

	void onProgress(const ProgressSnapshot& snapshot) override
	{
		const int width = _text->size.x;
		if (snapshot.currentEntry)
			_currentEntry = elidedInTheMiddle(toNativeSeparators(snapshot.currentEntry->value()), width);
		_entryBar = _unit == PrimaryProgressUnit::Bytes ? progressBar(currentEntryPercent(snapshot), width) : QString{};
		_overallBar = progressBar(overallPercent(snapshot, _unit), width);
		_status = elidedInTheMiddle(fileOperationProgressStatusText(snapshot, _operation, _unit), width);
		showProgress();
	}

	[[nodiscard]] Decision askDecision(const DecisionRequest& request) override
	{
		_questionAsked = true;
		return askForDecision(request, _operation);
	}

	[[nodiscard]] std::optional<ConflictResolutions> askConflictPlan(const ConflictPlan& plan) override
	{
		_questionAsked = true;
		return askForPlanAnswers(plan, _operation);
	}

	void onFinished(const OperationSummary& summary) override { _summary = summary; }

	void showProgress()
	{
		const QString status = _cancellationRequested ? QStringLiteral("Cancelling...") : _paused ? QStringLiteral("Paused") : _status;
		_text->setLines({ _currentEntry, _entryBar, QString{}, _overallBar, status });
	}

	void setPaused(const bool paused)
	{
		if (_paused == paused)
			return;

		_paused = paused;
		_job.setPaused(paused);

		delete[] const_cast<char*>(_pauseButton->title);
		_pauseButton->title = newStr(paused ? "~R~esume" : "~P~ause");
		_pauseButton->drawView();
		showProgress();
	}

	// Pauses the operation while asking
	void confirmCancellation()
	{
		if (_cancellationRequested)
			return;

		const bool wasPaused = _paused;
		setPaused(true);
		if (!confirmed(QStringLiteral("Cancel?"), QStringLiteral("Are you sure you want to cancel this operation?"),
			QStringLiteral("Cancel the operation"), QStringLiteral("Continue")))
		{
			setPaused(wasPaused);
			return;
		}

		_cancellationRequested = true;
		_job.cancel();
		_pauseButton->setState(sfDisabled, True);
		_cancelButton->setState(sfDisabled, True);
		showProgress();
	}

private:
	// Before _job: their initializers read the request that _job's initializer moves from
	const PromptOperation _operation;
	const PrimaryProgressUnit _unit;
	CFileOperationJob _job;

	// Owned by this dialog
	CTuiTextView* _text;
	TButton* _pauseButton;
	TButton* _cancelButton;

	QString _currentEntry, _entryBar, _overallBar, _status;
	std::optional<OperationSummary> _summary;
	bool _paused = false;
	bool _cancellationRequested = false;
	bool _questionAsked = false;
	bool _processingJobEvents = false;
};

// Returns when the operation has ended
[[nodiscard]] OperationSummary runOperation(FileOperationRequest request)
{
	using namespace std::chrono_literals;

	auto* dialog = new CTuiProgressDialog(std::move(request));
	dialog->start();

	// The progress window appears once the operation has run this long, or has asked a question: a short one never shows it
	const auto showTime = std::chrono::steady_clock::now() + 500ms;
	for (;;)
	{
		dialog->processJobEvents();
		if (dialog->summary() || dialog->questionAsked() || std::chrono::steady_clock::now() >= showTime)
			break;
		std::this_thread::sleep_for(10ms);
	}

	if (!dialog->summary())
		TProgram::deskTop->execView(dialog);

	assert_r(dialog->summary());
	OperationSummary summary = dialog->summary().value_or(OperationSummary{ .status = CompletionStatus::Failed });
	TObject::destroy(dialog);
	CController::get().refreshUnwatchedViews();
	return summary;
}

void showSummaryIfNeeded(const OperationSummary& summary, const PromptOperation operation)
{
	if (!fileOperationNeedsAttention(summary))
		return;

	DialogSpec report;
	report.title = fileOperationName(operation);
	// Elided, not wrapped: a diagnostic ends with its path
	for (const QString& line : fileOperationSummaryText(summary, operation).split(u'\n'))
		report.lines.push_back({ line, true });
	report.buttons = { QStringLiteral("OK") };
	report.defaultButton = report.focusedButton = 0;
	(void)execDialog(report);
}

void deletePermanently(const QStringList& paths)
{
	auto request = makePermanentDeleteRequest(paths);
	if (!request)
	{
		showMessage(QStringLiteral("Operation cannot start"), requestValidationErrorText(request.error()));
		return;
	}

	showSummaryIfNeeded(runOperation(std::move(*request)), PromptOperation::Delete);
}

void offerPermanentDeletion(const std::vector<OsShell::TrashFailure>& failures)
{
	DialogSpec offer;
	offer.title = QStringLiteral("Cannot move to trash");
	offer.lines.push_back({ failures.size() == 1 ? QStringLiteral("This item could not be moved to the trash. Delete it permanently?")
		: QStringLiteral("%1 items could not be moved to the trash. Delete them permanently?").arg(failures.size()) });
	offer.lines.push_back({});

	QStringList paths;
	for (const OsShell::TrashFailure& failure : failures)
	{
		paths.push_back(failure.path);
		offer.lines.push_back({ toNativeSeparators(failure.path) + QStringLiteral(": ") + failure.reason, true });
	}

	offer.buttons = { QStringLiteral("Delete permanently"), QStringLiteral("Cancel") };
	offer.focusedButton = 1; // An irreversible action must not be one stray Enter away
	if (execDialog(offer).button == size_t{ 0 })
		deletePermanently(paths);
}

// "name" for one item, "N items" for several
[[nodiscard]] QString itemsText(const std::vector<CFileSystemObject>& items)
{
	return items.size() == 1 ? QStringLiteral("\"%1\"").arg(items.front().fullName()) : QStringLiteral("%1 items").arg(items.size());
}

} // namespace

void transferSelectedItems(CController& controller, const Panel sourcePanel, const TransferKind kind)
{
	const std::vector<CFileSystemObject> sources = controller.panel(sourcePanel).selectedItemsOrCurrentItem();
	if (sources.empty())
		return;

	QStringList sourcePaths;
	std::vector<qulonglong> sourceHashes;
	for (const CFileSystemObject& source : sources)
	{
		sourcePaths.push_back(source.fullAbsolutePath());
		sourceHashes.push_back(source.hash());
	}

	const PromptOperation operation = kind == TransferKind::Copy ? PromptOperation::Copy : PromptOperation::Move;
	const QString operationName = fileOperationName(operation);
	const QString destinationFolder = controller.otherPanel(sourcePanel).currentDirPathNative();

	const std::optional<QString> destination = askForText(operationName, { QStringLiteral("%1 %2 to:").arg(operationName, itemsText(sources)), true },
		toNativeSeparators(prefillTransferDestination(kind, sourcePaths, destinationFolder)), operationName);
	if (!destination)
		return;

	auto request = makeUiTransferRequest(kind, sourcePaths, *destination, destinationFolder);
	if (!request)
	{
		showMessage(QStringLiteral("Operation cannot start"), requestValidationErrorText(request.error()));
		return;
	}

	const qulonglong tabId = controller.activeTabId(sourcePanel);
	const OperationSummary summary = runOperation(std::move(*request));
	// Only a copy deselects explicitly: a move removes its sources, which deselects them
	if (kind == TransferKind::Copy)
		controller.deselectCopiedItems(sourcePanel, tabId, sourceHashes, summary);
	showSummaryIfNeeded(summary, operation);
}

void deleteSelectedItems(CController& controller, const Panel sourcePanel, const bool toTrash)
{
	const std::vector<CFileSystemObject> items = controller.panel(sourcePanel).selectedItemsOrCurrentItem();
	if (items.empty())
		return;

	QStringList paths;
	for (const CFileSystemObject& item : items)
		paths.push_back(item.fullAbsolutePath());

	const DeletionBackend backend = deletionBackendFor(toTrash, DesktopShellUse::Never);
	if (backend == DeletionBackend::InternalJob)
	{
		if (confirmed(QStringLiteral("Delete"), QStringLiteral("Delete %1 permanently?").arg(itemsText(items)), QStringLiteral("Delete")))
			deletePermanently(paths);
		return;
	}

	assert_and_return_r(backend == DeletionBackend::QtTrash, );
	if (confirmed(QStringLiteral("Delete"), QStringLiteral("Move %1 to the trash?").arg(itemsText(items)), QStringLiteral("Move to trash")))
		controller.moveToTrash(std::move(paths), backend, [](const std::vector<OsShell::TrashFailure>& failures) { offerPermanentDeletion(failures); });
}
