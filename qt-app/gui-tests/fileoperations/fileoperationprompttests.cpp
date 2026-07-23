// WP10A: the typed decision prompt - exact controls per normative table row, scope visibility and
// validation, wording, and the returned Decision values.

#include "progressdialogs/cfileoperationprompt.h"

DISABLE_COMPILER_WARNINGS
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStringBuilder>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

#include <functional>

namespace
{

QString testPath(const char* name)
{
#ifdef _WIN32
	return QStringLiteral("C:/prompt-test/") % QLatin1String(name);
#else
	return QStringLiteral("/prompt-test/") % QLatin1String(name);
#endif
}

EntrySnapshot snapshot(const char* name, const OperationEntryKind kind, const uint64_t size = 0)
{
	const auto path = parseOperationPath(testPath(name));
	REQUIRE(path.has_value());
	return EntrySnapshot{ .path = *path, .kind = kind, .size = size };
}

DecisionRequest makeRequest(const IssueKind kind, EntrySnapshot source, std::optional<EntrySnapshot> destination = {},
	std::optional<FailureDetails> failure = {}, const bool scopeAllowed = true)
{
	return DecisionRequest{ OperationIssue{ kind, std::move(source), std::move(destination), std::move(failure) },
		allowedActionsFor(kind), scopeAllowed };
}

FailureDetails ioFailure(const FailedAction action, const QString& diagnostic = QStringLiteral("mock diagnostic"))
{
	return FailureDetails{ action, CFileSystemError{ FileErrorCategory::IoFailure, 5, diagnostic } };
}

// The dynamically created action buttons, in creation (= layout) order.
QStringList actionButtonNames(const CFileOperationPrompt& prompt)
{
	QStringList names;
	for (const auto* button : prompt.findChildren<QPushButton*>())
		names.push_back(button->objectName());
	return names;
}

void click(CFileOperationPrompt& prompt, const char* buttonName)
{
	auto* button = prompt.findChild<QPushButton*>(QLatin1String(buttonName));
	REQUIRE(button != nullptr);
	REQUIRE(button->isEnabled());
	button->click();
}

// Runs the real modal loop; the interaction is queued so it executes inside exec().
Decision askWith(CFileOperationPrompt& prompt, const std::function<void(CFileOperationPrompt&)>& interact)
{
	QTimer::singleShot(0, &prompt, [&] { interact(prompt); });
	return prompt.ask();
}

} // namespace

TEST_CASE("prompt: every normative row renders exactly the delivered actions, in order", "[fileoperationprompt]")
{
	using enum IssueKind;
	const struct
	{
		IssueKind kind;
		QStringList expectedButtons;
	} rows[] = {
		{ FileReplacement, { QStringLiteral("btnReplace"), QStringLiteral("btnRename"), QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
		{ RootDirectoryMerge, { QStringLiteral("btnMerge"), QStringLiteral("btnRename"), QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
		{ TypeMismatch, { QStringLiteral("btnRename"), QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
		{ ActionFailed, { QStringLiteral("btnRetry"), QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
		{ ReadOnlySourceRemoval, { QStringLiteral("btnMakeWritable"), QStringLiteral("btnRetry"), QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
		{ UnsupportedEntry, { QStringLiteral("btnSkip"), QStringLiteral("btnCancel") } },
	};

	for (const auto& row : rows)
	{
		const bool hasDestination = row.kind == FileReplacement || row.kind == RootDirectoryMerge || row.kind == TypeMismatch;
		const auto sourceKind = row.kind == RootDirectoryMerge ? OperationEntryKind::Directory : OperationEntryKind::RegularFile;
		auto request = makeRequest(row.kind,
			snapshot("src.bin", sourceKind, 700),
			hasDestination ? std::optional{ snapshot("dest.bin", sourceKind == OperationEntryKind::Directory ? OperationEntryKind::Directory : OperationEntryKind::RegularFile, 50) } : std::nullopt,
			row.kind == ActionFailed ? std::optional{ ioFailure(FailedAction::WriteDestination) } : std::nullopt);

		CFileOperationPrompt prompt{ request, PromptOperation::Move };
		INFO("IssueKind " << static_cast<int>(row.kind));
		CHECK(actionButtonNames(prompt) == row.expectedButtons);
	}
}

TEST_CASE("prompt: scope checkbox visibility", "[fileoperationprompt]")
{
	SECTION("offered when the request allows the scope and a rememberable action exists")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::FileReplacement,
			snapshot("src.bin", OperationEntryKind::RegularFile), snapshot("dest.bin", OperationEntryKind::RegularFile)), PromptOperation::Copy };
		auto* checkbox = prompt.findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"));
		REQUIRE(checkbox != nullptr);
		CHECK(!checkbox->isHidden());
		CHECK(!checkbox->text().isEmpty());
	}

	SECTION("hidden for an item-only request")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::RemovePublishedMoveSource), false), PromptOperation::Move };
		CHECK(prompt.findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"))->isHidden());
	}

	SECTION("ActionFailed scope wording says it covers further failures, not only this action")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination)), PromptOperation::Copy };
		const QString text = prompt.findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"))->text();
		CHECK(text.contains(QStringLiteral("failure"), Qt::CaseInsensitive));
		CHECK(!text.contains(QStringLiteral("writing"), Qt::CaseInsensitive));
	}
}

TEST_CASE("prompt: returned decisions", "[fileoperationprompt]")
{
	auto replacementRequest = [] {
		return makeRequest(IssueKind::FileReplacement,
			snapshot("src.bin", OperationEntryKind::RegularFile, 700), snapshot("dest.bin", OperationEntryKind::RegularFile, 50));
	};

	SECTION("a plain action defaults to this-item scope")
	{
		CFileOperationPrompt prompt{ replacementRequest(), PromptOperation::Copy };
		const Decision decision = askWith(prompt, [](auto& p) { click(p, "btnReplace"); });
		CHECK(decision.action == DecisionAction::Replace);
		CHECK(decision.scope == DecisionScope::ThisItem);
		CHECK(!decision.newName.has_value());
	}

	SECTION("a checked scope box broadens a rememberable action")
	{
		CFileOperationPrompt prompt{ replacementRequest(), PromptOperation::Copy };
		const Decision decision = askWith(prompt, [](auto& p) {
			p.template findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"))->setChecked(true);
			click(p, "btnReplace");
		});
		CHECK(decision.action == DecisionAction::Replace);
		CHECK(decision.scope == DecisionScope::RemainingMatchingIssues);
	}

	SECTION("a checked scope box never broadens a non-rememberable action")
	{
		CFileOperationPrompt prompt{ replacementRequest(), PromptOperation::Copy };
		const Decision decision = askWith(prompt, [](auto& p) {
			p.template findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"))->setChecked(true);
			p.template findChild<QLineEdit*>(QStringLiteral("renameEdit"))->setText(QStringLiteral("renamed.bin"));
			click(p, "btnRename");
		});
		CHECK(decision.action == DecisionAction::Rename);
		CHECK(decision.scope == DecisionScope::ThisItem);
		REQUIRE(decision.newName.has_value());
		CHECK(*decision.newName == QStringLiteral("renamed.bin"));
	}

	SECTION("closing the dialog without choosing is cancellation, this item only")
	{
		CFileOperationPrompt prompt{ replacementRequest(), PromptOperation::Copy };
		const Decision closed = askWith(prompt, [](auto& p) {
			p.template findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"))->setChecked(true);
			p.reject(); // The Esc/close path
		});
		CHECK(closed.action == DecisionAction::Cancel);
		CHECK(closed.scope == DecisionScope::ThisItem);
	}

	SECTION("the Cancel button matches the close path")
	{
		CFileOperationPrompt prompt{ replacementRequest(), PromptOperation::Copy };
		const Decision decision = askWith(prompt, [](auto& p) { click(p, "btnCancel"); });
		CHECK(decision.action == DecisionAction::Cancel);
		CHECK(decision.scope == DecisionScope::ThisItem);
	}
}

TEST_CASE("prompt: rename controls and validation", "[fileoperationprompt]")
{
	CFileOperationPrompt prompt{ makeRequest(IssueKind::FileReplacement,
		snapshot("src.bin", OperationEntryKind::RegularFile), snapshot("dest.bin", OperationEntryKind::RegularFile)), PromptOperation::Copy };

	auto* edit = prompt.findChild<QLineEdit*>(QStringLiteral("renameEdit"));
	auto* renameButton = prompt.findChild<QPushButton*>(QStringLiteral("btnRename"));
	REQUIRE(edit != nullptr);
	REQUIRE(renameButton != nullptr);

	SECTION("prefilled with the current name, which is not an acceptable new name")
	{
		CHECK(edit->text() == QStringLiteral("src.bin"));
		CHECK(!renameButton->isEnabled());
	}

	SECTION("invalid names disable the button")
	{
		for (const char* invalid : { "", "   ", "a/b.bin", "a\\b.bin", ".", ".." })
		{
			edit->setText(QLatin1String(invalid));
			INFO("name: '" << invalid << "'");
			CHECK(!renameButton->isEnabled());
		}
	}

	SECTION("a case-only respell is a real rename and enables the button")
	{
		edit->setText(QStringLiteral("SRC.BIN"));
		CHECK(renameButton->isEnabled());
	}

	SECTION("a valid different name enables the button; surrounding whitespace is trimmed in the decision")
	{
		edit->setText(QStringLiteral("  renamed.bin  "));
		CHECK(renameButton->isEnabled());

		const Decision decision = askWith(prompt, [](auto& p) { click(p, "btnRename"); });
		CHECK(decision.action == DecisionAction::Rename);
		REQUIRE(decision.newName.has_value());
		CHECK(*decision.newName == QStringLiteral("renamed.bin"));
	}

	SECTION("the rename row does not exist for issues that cannot rename")
	{
		CFileOperationPrompt failedPrompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination)), PromptOperation::Copy };
		CHECK(failedPrompt.findChild<QLineEdit*>(QStringLiteral("renameEdit"))->isHidden());
		CHECK(failedPrompt.findChild<QPushButton*>(QStringLiteral("btnRename")) == nullptr);
	}
}

TEST_CASE("prompt: wording", "[fileoperationprompt]")
{
	auto questionOf = [](const CFileOperationPrompt& prompt) {
		return prompt.findChild<QLabel*>(QStringLiteral("lblQuestion"))->text();
	};

	SECTION("type mismatch names both kinds, both directions")
	{
		CFileOperationPrompt fileOntoFolder{ makeRequest(IssueKind::TypeMismatch,
			snapshot("src.bin", OperationEntryKind::RegularFile), snapshot("dest", OperationEntryKind::Directory)), PromptOperation::Copy };
		CHECK(questionOf(fileOntoFolder).contains(QStringLiteral("a file")));
		CHECK(questionOf(fileOntoFolder).contains(QStringLiteral("folder")));

		CFileOperationPrompt folderOntoFile{ makeRequest(IssueKind::TypeMismatch,
			snapshot("src", OperationEntryKind::Directory), snapshot("dest.bin", OperationEntryKind::RegularFile)), PromptOperation::Copy };
		CHECK(questionOf(folderOntoFile).contains(QStringLiteral("a folder")));
		CHECK(questionOf(folderOntoFile).contains(QStringLiteral("file")));

		CFileOperationPrompt folderOntoLink{ makeRequest(IssueKind::TypeMismatch,
			snapshot("src", OperationEntryKind::Directory), snapshot("dest", OperationEntryKind::DirectoryLink)), PromptOperation::Copy };
		CHECK(questionOf(folderOntoLink).contains(QStringLiteral("link to a folder")));
	}

	SECTION("every failed action renders a distinct, specific description")
	{
		using enum FailedAction;
		QSet<QString> questions;
		int actionCount = 0;
		for (const FailedAction action : { InspectSource, InspectDestination, ReadSource, CreateDestinationDirectory,
			PrepareStagingFile, WriteDestination, PreserveFileMetadata, PublishDestination, RenameEntry, MakeWritable,
			RemoveEntry, RemovePublishedMoveSource, CleanupStaging, PreserveDirectoryTimestamps })
		{
			CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
				{}, ioFailure(action)), PromptOperation::Move };
			const QString question = questionOf(prompt);
			CHECK(question.contains(QStringLiteral("failed")));
			questions.insert(question);
			++actionCount;
		}
		CHECK(questions.size() == actionCount); // No two actions share a wording
	}

	SECTION("the failure reason includes the supplied diagnostic")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination, QStringLiteral("mock diagnostic"))), PromptOperation::Copy };
		const QString failureText = prompt.findChild<QLabel*>(QStringLiteral("lblFailure"))->text();
		CHECK(failureText.contains(QStringLiteral("mock diagnostic")));
	}

	SECTION("read-only removal is labeled by its actual consequence; no force-remove exists")
	{
		CFileOperationPrompt movePrompt{ makeRequest(IssueKind::ReadOnlySourceRemoval,
			snapshot("src.bin", OperationEntryKind::RegularFile)), PromptOperation::Move };
		CHECK(movePrompt.findChild<QPushButton*>(QStringLiteral("btnMakeWritable"))->text() == QStringLiteral("Make writable and move"));

		CFileOperationPrompt deletePrompt{ makeRequest(IssueKind::ReadOnlySourceRemoval,
			snapshot("src.bin", OperationEntryKind::RegularFile)), PromptOperation::Delete };
		CHECK(deletePrompt.findChild<QPushButton*>(QStringLiteral("btnMakeWritable"))->text() == QStringLiteral("Make writable and delete"));
	}

	SECTION("the raced read-only form presents its failure")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ReadOnlySourceRemoval, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::RemoveEntry), false), PromptOperation::Move };
		auto* failureLabel = prompt.findChild<QLabel*>(QStringLiteral("lblFailure"));
		CHECK(!failureLabel->isHidden());
		CHECK(failureLabel->text().contains(QStringLiteral("failed")));
	}

	SECTION("the failure label format distinguishes ActionFailed from a raced failure")
	{
		// The ActionFailed headline already names the action, so the label carries only the reason.
		CFileOperationPrompt failedPrompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination)), PromptOperation::Copy };
		CHECK(failedPrompt.findChild<QLabel*>(QStringLiteral("lblFailure"))->text().startsWith(QStringLiteral("Reason:")));

		// A raced failure on another kind must name the failed action itself.
		CFileOperationPrompt racedPrompt{ makeRequest(IssueKind::ReadOnlySourceRemoval, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::RemoveEntry), false), PromptOperation::Move };
		const QString racedText = racedPrompt.findChild<QLabel*>(QStringLiteral("lblFailure"))->text();
		CHECK(!racedText.startsWith(QStringLiteral("Reason:")));
		CHECK(racedText.contains(QStringLiteral(" failed: ")));
	}

	SECTION("the unsupported-entry question names the operation")
	{
		CFileOperationPrompt movePrompt{ makeRequest(IssueKind::UnsupportedEntry, snapshot("pipe", OperationEntryKind::Other)), PromptOperation::Move };
		CHECK(questionOf(movePrompt).contains(QStringLiteral("moved")));

		CFileOperationPrompt copyPrompt{ makeRequest(IssueKind::UnsupportedEntry, snapshot("pipe", OperationEntryKind::Other)), PromptOperation::Copy };
		CHECK(questionOf(copyPrompt).contains(QStringLiteral("copied")));
	}

	SECTION("the read-only question explains source removal only for a move")
	{
		CFileOperationPrompt movePrompt{ makeRequest(IssueKind::ReadOnlySourceRemoval,
			snapshot("src.bin", OperationEntryKind::RegularFile)), PromptOperation::Move };
		CHECK(questionOf(movePrompt).contains(QStringLiteral("requires removing the source")));

		CFileOperationPrompt deletePrompt{ makeRequest(IssueKind::ReadOnlySourceRemoval,
			snapshot("src.bin", OperationEntryKind::RegularFile)), PromptOperation::Delete };
		CHECK(questionOf(deletePrompt) == QStringLiteral("The file is read-only."));
	}

	SECTION("each issue kind carries its own scope label")
	{
		using enum IssueKind;
		const struct
		{
			IssueKind kind;
			QString expectedFragment;
		} rows[] = {
			{ FileReplacement, QStringLiteral("file collisions") },
			{ RootDirectoryMerge, QStringLiteral("folder collisions") },
			{ TypeMismatch, QStringLiteral("type mismatches") },
			{ ReadOnlySourceRemoval, QStringLiteral("read-only items") },
			{ UnsupportedEntry, QStringLiteral("unsupported entries") },
		};

		for (const auto& row : rows)
		{
			const bool hasDestination = row.kind == FileReplacement || row.kind == RootDirectoryMerge || row.kind == TypeMismatch;
			const auto sourceKind = row.kind == RootDirectoryMerge ? OperationEntryKind::Directory : OperationEntryKind::RegularFile;
			CFileOperationPrompt prompt{ makeRequest(row.kind, snapshot("src", sourceKind),
				hasDestination ? std::optional{ snapshot("dest", sourceKind) } : std::nullopt), PromptOperation::Move };

			INFO("IssueKind " << static_cast<int>(row.kind));
			auto* checkbox = prompt.findChild<QCheckBox*>(QStringLiteral("scopeCheckBox"));
			REQUIRE(checkbox != nullptr);
			CHECK(!checkbox->isHidden());
			CHECK(checkbox->text().contains(row.expectedFragment));
		}
	}

	SECTION("committed-cleanup prompts spell out the Cancel consequences")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::RemovePublishedMoveSource), false), PromptOperation::Move };
		auto* consequences = prompt.findChild<QLabel*>(QStringLiteral("lblConsequences"));
		CHECK(!consequences->isHidden());
		CHECK(consequences->text().contains(QStringLiteral("already moved keep their new location")));
		CHECK(consequences->text().contains(QStringLiteral("remaining items")));

		// An ordinary request carries no such warning.
		CFileOperationPrompt ordinary{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination)), PromptOperation::Move };
		CHECK(ordinary.findChild<QLabel*>(QStringLiteral("lblConsequences"))->isHidden());
	}
}

TEST_CASE("prompt: entry info rendering", "[fileoperationprompt]")
{
	SECTION("destination block absent when the issue has no destination")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::ActionFailed, snapshot("src.bin", OperationEntryKind::RegularFile),
			{}, ioFailure(FailedAction::WriteDestination)), PromptOperation::Copy };
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblDestinationPath"))->isHidden());
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblDestinationCaption"))->isHidden());
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblSourcePath"))->text().contains(QStringLiteral("src.bin")));
	}

	SECTION("both blocks shown for a collision, with the entry kinds named")
	{
		CFileOperationPrompt prompt{ makeRequest(IssueKind::FileReplacement,
			snapshot("src.bin", OperationEntryKind::RegularFile, 700), snapshot("dest.bin", OperationEntryKind::DirectoryLink)), PromptOperation::Copy };
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblDestinationPath"))->text().contains(QStringLiteral("dest.bin")));
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblSourceDetails"))->text().contains(QStringLiteral("file")));
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblDestinationDetails"))->text().contains(QStringLiteral("link to a folder")));
		// A destination link is a file-like replaceable entry, and the headline names its real kind.
		CHECK(prompt.findChild<QLabel*>(QStringLiteral("lblQuestion"))->text().contains(QStringLiteral("link to a folder")));
	}
}
