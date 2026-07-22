#include "cfileoperationprompt.h"
#include "filesystemhelperfunctions.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfileoperationprompt.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace
{

QString kindNoun(const OperationEntryKind kind)
{
	using enum OperationEntryKind;
	switch (kind)
	{
	case RegularFile: return QObject::tr("file");
	case Directory: return QObject::tr("folder");
	case FileLink: return QObject::tr("link to a file");
	case DirectoryLink: return QObject::tr("link to a folder");
	case Other: return QObject::tr("special entry");
	}
	return {};
}

QString failedActionDescription(const FailedAction action)
{
	using enum FailedAction;
	switch (action)
	{
	case InspectSource: return QObject::tr("Reading the source entry's properties");
	case InspectDestination: return QObject::tr("Reading the destination entry's properties");
	case ReadSource: return QObject::tr("Reading the source file");
	case CreateDestinationDirectory: return QObject::tr("Creating the destination folder");
	case PrepareStagingFile: return QObject::tr("Creating the temporary destination file");
	case WriteDestination: return QObject::tr("Writing the destination file");
	case PreserveFileMetadata: return QObject::tr("Preserving the file's attributes");
	case PublishDestination: return QObject::tr("Finalizing the destination file");
	case RenameEntry: return QObject::tr("Moving the entry into place");
	case MakeWritable: return QObject::tr("Making the entry writable");
	case RemoveEntry: return QObject::tr("Removing the entry");
	case RemovePublishedMoveSource: return QObject::tr("Removing the moved entry's source");
	case CleanupStaging: return QObject::tr("Removing the temporary destination file");
	case PreserveDirectoryTimestamps: return QObject::tr("Preserving the folder's timestamps");
	}
	return {};
}

QString categoryDescription(const FileErrorCategory category)
{
	using enum FileErrorCategory;
	switch (category)
	{
	case NotFound: return QObject::tr("the entry does not exist");
	case AlreadyExists: return QObject::tr("an entry with this name already exists");
	case CrossDevice: return QObject::tr("the destination is on a different volume");
	case ReadOnly: return QObject::tr("the entry is read-only");
	case PermissionDenied: return QObject::tr("access denied");
	case NotEnoughSpace: return QObject::tr("not enough free space");
	case Unsupported: return QObject::tr("the operation is not supported here");
	case IoFailure: return QObject::tr("an input/output error occurred");
	}
	return {};
}

QString errorText(const CFileSystemError& error)
{
	QString text = categoryDescription(error.category);
	if (!error.diagnostic.isEmpty())
		text += QLatin1String(" (") % error.diagnostic % QLatin1Char(')');
	else if (error.nativeCode != 0)
		text += QObject::tr(" (error code %1)").arg(error.nativeCode);
	return text;
}

QString entryDetails(const EntrySnapshot& entry)
{
	QString details = kindNoun(entry.kind);
	if (entry.kind == OperationEntryKind::RegularFile || entry.kind == OperationEntryKind::FileLink)
		details += QLatin1String(", ") % fileSizeToString(entry.size);

	// Display-only re-inspection: the snapshot deliberately carries no timestamps.
	const QFileInfo info{ entry.path.value() };
	if (info.exists())
		details += QObject::tr(", modified %1").arg(info.lastModified().toString(QStringLiteral("dd.MM.yyyy hh:mm")));
	return details;
}

QString buttonName(const DecisionAction action)
{
	using enum DecisionAction;
	switch (action)
	{
	case Skip: return QStringLiteral("btnSkip");
	case Replace: return QStringLiteral("btnReplace");
	case Merge: return QStringLiteral("btnMerge");
	case MakeWritable: return QStringLiteral("btnMakeWritable");
	case Rename: return QStringLiteral("btnRename");
	case Retry: return QStringLiteral("btnRetry");
	case Cancel: return QStringLiteral("btnCancel");
	}
	return {};
}

} // namespace

CFileOperationPrompt::CFileOperationPrompt(const DecisionRequest& request, const PromptOperation operation, QWidget* parent) :
	QDialog{ parent },
	ui{ new Ui::CFileOperationPrompt },
	_request{ request },
	_operation{ operation }
{
	ui->setupUi(this);

	switch (_operation)
	{
	case PromptOperation::Copy: setWindowTitle(tr("Copy")); break;
	case PromptOperation::Move: setWindowTitle(tr("Move")); break;
	case PromptOperation::Delete: setWindowTitle(tr("Delete")); break;
	}

	ui->lblQuestion->setText(questionText());
	setupEntryInfo();
	setupAuxiliaryTexts();
	createActionButtons();
	if (_renameButton)
		updateRenameControls();
}

CFileOperationPrompt::~CFileOperationPrompt()
{
	delete ui;
}

Decision CFileOperationPrompt::ask()
{
	adjustSize();
	exec();
	return _decision;
}

void CFileOperationPrompt::onActionChosen(const DecisionAction action)
{
	const bool remember = !ui->scopeCheckBox->isHidden() && ui->scopeCheckBox->isChecked()
		&& isActionRememberable(_request.issue.kind, action);
	_decision = Decision{ .action = action, .scope = remember ? DecisionScope::RemainingMatchingIssues : DecisionScope::ThisItem, .newName = {} };
	if (action == DecisionAction::Rename)
		_decision.newName = ui->renameEdit->text().trimmed();
	accept();
}

void CFileOperationPrompt::updateRenameControls()
{
	const QString name = ui->renameEdit->text().trimmed();
	// An unchanged name is not a rename; an exact-case respell of the same name is.
	_renameButton->setEnabled(isValidEntryName(name) && name != _request.issue.source.path.name());
}

void CFileOperationPrompt::createActionButtons()
{
	for (const DecisionAction action : _request.allowedActions)
	{
		auto* button = new QPushButton{ actionLabel(action), this };
		button->setObjectName(buttonName(action));
		button->setAutoDefault(false);
		// Enter must not trigger a mutating action by accident; Cancel is the only default.
		button->setDefault(action == DecisionAction::Cancel);
		connect(button, &QPushButton::clicked, this, [this, action] { onActionChosen(action); });
		ui->buttonsLayout->addWidget(button);
		if (action == DecisionAction::Rename)
			_renameButton = button;
	}
}

void CFileOperationPrompt::setupEntryInfo()
{
	const OperationIssue& issue = _request.issue;

	ui->lblSourcePath->setText(QDir::toNativeSeparators(issue.source.path.value()));
	ui->lblSourceDetails->setText(entryDetails(issue.source));

	if (issue.destination)
	{
		ui->lblDestinationPath->setText(QDir::toNativeSeparators(issue.destination->path.value()));
		ui->lblDestinationDetails->setText(entryDetails(*issue.destination));
	}
	else
	{
		ui->lblDestinationCaption->hide();
		ui->lblDestinationPath->hide();
		ui->lblDestinationDetails->hide();
	}
}

void CFileOperationPrompt::setupAuxiliaryTexts()
{
	const OperationIssue& issue = _request.issue;

	if (issue.failure)
	{
		// For ActionFailed the headline already names the attempted action; other kinds carry a raced
		// failure and present it in full.
		ui->lblFailure->setText(issue.kind == IssueKind::ActionFailed
			? tr("Reason: %1").arg(errorText(issue.failure->filesystemError))
			: tr("%1 failed: %2").arg(failedActionDescription(issue.failure->action), errorText(issue.failure->filesystemError)));
	}
	else
		ui->lblFailure->hide();

	// A request that disallows the remaining-matching scope is a committed-cleanup prompt: publication has
	// already succeeded, so Cancel's consequences are worth spelling out.
	if (!_request.remainingMatchingScopeAllowed)
		ui->lblConsequences->setText(tr("Cancelling stops the operation: items already moved keep their new location, "
			"this item keeps both its source and its published destination, and the remaining items are left untouched."));
	else
		ui->lblConsequences->hide();

	const bool scopeOffered = _request.remainingMatchingScopeAllowed
		&& std::ranges::any_of(_request.allowedActions, [&issue](const DecisionAction action) { return isActionRememberable(issue.kind, action); });
	if (scopeOffered)
		ui->scopeCheckBox->setText(scopeLabel());
	else
		ui->scopeCheckBox->hide();

	if (std::ranges::find(_request.allowedActions, DecisionAction::Rename) != _request.allowedActions.end())
	{
		ui->renameEdit->setText(issue.source.path.name());
		connect(ui->renameEdit, &QLineEdit::textChanged, this, &CFileOperationPrompt::updateRenameControls);
	}
	else
	{
		ui->lblRenameCaption->hide();
		ui->renameEdit->hide();
	}
}

QString CFileOperationPrompt::questionText() const
{
	const OperationIssue& issue = _request.issue;
	// Kinds that require a destination snapshot are guarded anyway: the prompt renders, it does not validate.
	const OperationEntryKind destinationKind = issue.destination ? issue.destination->kind : OperationEntryKind::RegularFile;

	using enum IssueKind;
	switch (issue.kind)
	{
	case FileReplacement:
		return tr("The destination %1 already exists.").arg(kindNoun(destinationKind));
	case RootDirectoryMerge:
		return tr("The destination folder already exists. Merge the source folder's contents into it?");
	case TypeMismatch:
		return tr("The source is a %1, but the existing destination is a %2. An entry cannot replace an entry of a different type.")
			.arg(kindNoun(issue.source.kind), kindNoun(destinationKind));
	case ActionFailed:
		return issue.failure ? tr("%1 failed.").arg(failedActionDescription(issue.failure->action)) : tr("The operation failed.");
	case ReadOnlySourceRemoval:
		return _operation == PromptOperation::Move
			? tr("The source file is read-only, and moving it requires removing the source.")
			: tr("The file is read-only.");
	case UnsupportedEntry:
		return tr("This entry is not a file or a folder (a device, pipe, or socket) and cannot be %1.")
			.arg(_operation == PromptOperation::Move ? tr("moved") : tr("copied"));
	}

	return {};
}

QString CFileOperationPrompt::actionLabel(const DecisionAction action) const
{
	using enum DecisionAction;
	switch (action)
	{
	case Skip: return tr("Skip");
	case Replace: return tr("Replace");
	case Merge: return tr("Merge");
	case MakeWritable:
		return _operation == PromptOperation::Move ? tr("Make writable and move") : tr("Make writable and delete");
	case Rename: return tr("Rename");
	case Retry: return tr("Retry");
	case Cancel: return tr("Cancel");
	}
	return {};
}

QString CFileOperationPrompt::scopeLabel() const
{
	using enum IssueKind;
	switch (_request.issue.kind)
	{
	case FileReplacement: return tr("Apply to all remaining file collisions");
	case RootDirectoryMerge: return tr("Apply to all remaining folder collisions");
	case TypeMismatch: return tr("Apply to all remaining type mismatches");
	// The remembered Skip covers any later failure, not only the currently named action - say so.
	case ActionFailed: return tr("Apply to any further failures, not only this one");
	case ReadOnlySourceRemoval: return tr("Apply to all remaining read-only items");
	case UnsupportedEntry: return tr("Apply to all remaining unsupported entries");
	}
	return {};
}
