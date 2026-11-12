#include "controllertesthelpers.h"

#include <vector>

TEST_CASE("CController - tabs are restored as they were saved", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;
	const QString folderA = tree.makeDir(QStringLiteral("a"));
	const QString fileInA = tree.makeFile(QStringLiteral("a/a.txt"));
	const QString folderB = tree.makeDir(QStringLiteral("b"));
	const QString fileInB = tree.makeFile(QStringLiteral("b/b.txt"));

	std::vector<qulonglong> savedIds;
	qulonglong idA = 0, idB = 0;
	{
		ControllerSession session;
		idA = session->addTab(Panel::LeftPanel, folderA);
		session->setCurrentItemHashForCurrentFolder(Panel::LeftPanel, hashOf(fileInA), false);
		idB = session->addTab(Panel::LeftPanel, folderB);
		session->setCurrentItemHashForCurrentFolder(Panel::LeftPanel, hashOf(fileInB), false);
		// To the middle: a restore that lost the active index would activate the first or the last tab
		session->moveTabPosition(Panel::LeftPanel, idB, 1);
		savedIds = session->tabIds(Panel::LeftPanel);
	}

	ControllerSession session;
	CHECK(session->tabIds(Panel::LeftPanel) == savedIds);
	CHECK(session->activeTabId(Panel::LeftPanel) == idB);
	CHECK(session->tabPath(Panel::LeftPanel, idA) == folderA + '/');
	CHECK(session->tabPath(Panel::LeftPanel, idB) == folderB + '/');
	// The cursor is saved with a separator after the folder and looked up without one
	CHECK(session->tabById(Panel::LeftPanel, idA).currentItemHashForFolder(folderA) == hashOf(fileInA));
	CHECK(session->tabById(Panel::LeftPanel, idB).currentItemHashForFolder(folderB) == hashOf(fileInB));
}

TEST_CASE("CController - a background tab keeps a folder it cannot reach through restarts", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;
	// Never created: stands for a folder on a share that is asleep when the application starts
	const QString unreachable = tree.path(QStringLiteral("asleep"));

	qulonglong tabId = 0;
	{
		ControllerSession session;
		tabId = session->addTab(Panel::LeftPanel, unreachable, false);
		CHECK(session->tabPath(Panel::LeftPanel, tabId) == unreachable + '/');
	}

	// Restored, then saved again on exit without being activated
	{
		ControllerSession session;
		CHECK(session->tabPath(Panel::LeftPanel, tabId) == unreachable + '/');
	}

	ControllerSession session;
	CHECK(session->tabPath(Panel::LeftPanel, tabId) == unreachable + '/');
}

TEST_CASE("CController - the id of a closed tab is not given to a tab opened after a restart", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;

	qulonglong closedId = 0;
	{
		ControllerSession session;
		closedId = session->addTab(Panel::LeftPanel, tree.path());
		session->closeTab(Panel::LeftPanel, closedId);
	}

	ControllerSession session;
	CHECK(session->addTab(Panel::LeftPanel, tree.path()) != closedId);
}

TEST_CASE("CController - the instance closed last saves its tabs, though they did not change", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;
	const QString mine = tree.makeDir(QStringLiteral("mine"));
	const QString other = tree.makeDir(QStringLiteral("other"));

	{
		ControllerSession session;
		session->addTab(Panel::LeftPanel, mine); // Saved now
		// Another instance sharing the settings saves its own tabs
		QSettings{}.setValue(KEY_LPANEL_TABS, QStringList{ other + '/' });
	}

	ControllerSession session;
	CHECK(session->tabPath(Panel::LeftPanel, session->activeTabId(Panel::LeftPanel)) == mine + '/');
}

TEST_CASE("CController - the superseded tab id counter is removed from the settings at exit", "[controller][persistence]")
{
	clearSavedState();
	QSettings{}.setValue(KEY_NEXT_TAB_ID, 7);
	{
		ControllerSession session;
	}
	CHECK_FALSE(QSettings{}.contains(KEY_NEXT_TAB_ID));
}

TEST_CASE("CController - saved tab records are read in all of their formats", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;
	const QString folderA = tree.makeDir(QStringLiteral("a"));
	const QString folderB = tree.makeDir(QStringLiteral("b"));
	const QString folderC = tree.makeDir(QStringLiteral("c"));
	const QString fileInA = tree.makeFile(QStringLiteral("a/a.txt"));

	{
		QSettings settings;
		// "<id>:<cursor hash>:<path>" from before the sorting was saved, a bare path from before tabs had ids,
		// and "<id>:<cursor hash>:<sort field>:<sort order>:<path>"
		settings.setValue(KEY_LPANEL_TABS, QStringList{ QStringLiteral("7:%1:%2/").arg(hashOf(fileInA)).arg(folderA), folderB + '/',
			QStringLiteral("9:0:%1:%2:%3/").arg((int)FileListSortField::Size).arg((int)Qt::DescendingOrder).arg(folderC) });
		settings.setValue(KEY_LPANEL_ACTIVE_TAB, 1);
	}

	ControllerSession session;
	const std::vector<qulonglong> ids = session->tabIds(Panel::LeftPanel);
	REQUIRE(ids.size() == 3);
	CHECK(ids[0] == 7u);
	CHECK((ids[1] != 0u && ids[1] != 7u && ids[1] != 9u));
	CHECK(ids[2] == 9u);
	CHECK(session->activeTabId(Panel::LeftPanel) == ids[1]);
	CHECK(session->tabPath(Panel::LeftPanel, ids[0]) == folderA + '/');
	CHECK(session->tabPath(Panel::LeftPanel, ids[1]) == folderB + '/');
	CHECK(session->tabPath(Panel::LeftPanel, ids[2]) == folderC + '/');
	CHECK(session->tabById(Panel::LeftPanel, ids[0]).currentItemHashForFolder(folderA) == hashOf(fileInA));

	CHECK(session->tabById(Panel::LeftPanel, ids[0]).sortField() == FileListSortField::Extension);
	CHECK(session->tabById(Panel::LeftPanel, ids[0]).sortOrder() == Qt::AscendingOrder);
	CHECK(session->tabById(Panel::LeftPanel, ids[2]).sortField() == FileListSortField::Size);
	CHECK(session->tabById(Panel::LeftPanel, ids[2]).sortOrder() == Qt::DescendingOrder);
}

TEST_CASE("CController - a tab's sorting is restored with it", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;

	qulonglong tabId = 0;
	{
		ControllerSession session;
		tabId = session->addTab(Panel::LeftPanel, tree.path());
		session->setTabSorting(Panel::LeftPanel, tabId, FileListSortField::Date, Qt::DescendingOrder);
	}

	ControllerSession session;
	CHECK(session->tabById(Panel::LeftPanel, tabId).sortField() == FileListSortField::Date);
	CHECK(session->tabById(Panel::LeftPanel, tabId).sortOrder() == Qt::DescendingOrder);
}
