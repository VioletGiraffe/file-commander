#include "controllertesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QDeadlineTimer>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("CController - a copy deselects its sources only when it completed with nothing failed or skipped", "[controller][selection]")
{
	clearSavedState();
	TempTree tree;
	const qulonglong file = CFileSystemObject{ tree.makeFile(QStringLiteral("a.txt")) }.hash();
	ControllerSession session;

	REQUIRE(session->setPath(Panel::LeftPanel, tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	const QDeadlineTimer deadline{ 10s };
	while (!session->panel(Panel::LeftPanel).itemHashExists(file) && !deadline.hasExpired())
	{
		session->uiThreadTimerTick();
		std::this_thread::sleep_for(10ms);
	}
	REQUIRE(session->panel(Panel::LeftPanel).itemHashExists(file));

	CPanel& panel = session->panel(Panel::LeftPanel);
	const qulonglong tabId = session->activeTabId(Panel::LeftPanel);
	panel.setItemsSelected({ file }, true);
	REQUIRE(panel.selectedItemHashes() == std::vector<qulonglong>{ file });

	for (const OperationSummary& unfinished : { OperationSummary{ .status = CompletionStatus::Cancelled, .completedItems = 1 },
		OperationSummary{ .status = CompletionStatus::Completed, .skippedItems = 1 }, OperationSummary{ .status = CompletionStatus::Completed, .failedItems = 1 } })
	{
		session->deselectCopiedItems(Panel::LeftPanel, tabId, { file }, unfinished);
		CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ file });
	}

	session->deselectCopiedItems(Panel::LeftPanel, tabId, { file }, OperationSummary{ .status = CompletionStatus::Completed, .completedItems = 1, .warningCount = 1 });
	CHECK(panel.selectedItemHashes().empty());
}
