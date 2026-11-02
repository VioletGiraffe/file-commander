#include "controllertesthelpers.h"

#include <algorithm>
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

	// No restored tab has the closed tab's id, so only the saved id counter can rule it out
	ControllerSession session;
	CHECK(session->addTab(Panel::LeftPanel, tree.path()) > closedId);
}

TEST_CASE("CController - without the saved id counter, a new tab still gets an id no restored tab has", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;

	{
		ControllerSession session;
		session->addTab(Panel::LeftPanel, tree.path());
		session->addTab(Panel::RightPanel, tree.path());
	}
	// As after an abrupt exit that saved the tabs but not the counter
	QSettings{}.remove(KEY_NEXT_TAB_ID);

	ControllerSession session;
	std::vector<qulonglong> restoredIds = session->tabIds(Panel::LeftPanel);
	const std::vector<qulonglong> rightIds = session->tabIds(Panel::RightPanel);
	restoredIds.insert(restoredIds.end(), rightIds.begin(), rightIds.end());

	CHECK(session->addTab(Panel::LeftPanel, tree.path()) > std::ranges::max(restoredIds));
}

TEST_CASE("CController - saved tab records are read in both of their formats", "[controller][persistence]")
{
	clearSavedState();
	TempTree tree;
	const QString folderA = tree.makeDir(QStringLiteral("a"));
	const QString folderB = tree.makeDir(QStringLiteral("b"));
	const QString fileInA = tree.makeFile(QStringLiteral("a/a.txt"));

	{
		QSettings settings;
		// "<id>:<cursor hash>:<path>", and a bare path from before tabs had ids
		settings.setValue(KEY_LPANEL_TABS, QStringList{ QStringLiteral("7:%1:%2/").arg(hashOf(fileInA)).arg(folderA), folderB + '/' });
		settings.setValue(KEY_LPANEL_ACTIVE_TAB, 1);
		// A counter that fell behind the saved ids: the record without an id must still not get id 7
		settings.setValue(KEY_NEXT_TAB_ID, 7);
	}

	ControllerSession session;
	const std::vector<qulonglong> ids = session->tabIds(Panel::LeftPanel);
	REQUIRE(ids.size() == 2);
	CHECK(ids[0] == 7u);
	CHECK(ids[1] > 7u);
	CHECK(session->activeTabId(Panel::LeftPanel) == ids[1]);
	CHECK(session->tabPath(Panel::LeftPanel, ids[0]) == folderA + '/');
	CHECK(session->tabPath(Panel::LeftPanel, ids[1]) == folderB + '/');
	CHECK(session->tabById(Panel::LeftPanel, ids[0]).currentItemHashForFolder(folderA) == hashOf(fileInA));
}
