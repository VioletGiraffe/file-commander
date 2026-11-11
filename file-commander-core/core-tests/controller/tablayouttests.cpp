#include "controllertesthelpers.h"

#include <algorithm>
#include <vector>

// Up to 5 tabs: every operation saves the tab state, and 5 already covers a tab with two neighbours on either side
constexpr size_t MaxTabCount = 5;

static void addBackgroundTabsUpTo(ControllerSession& session, size_t tabCount, const QString& folder)
{
	while ((size_t)session->tabCount(Panel::LeftPanel) < tabCount)
		session->addTab(Panel::LeftPanel, folder, false);
}

TEST_CASE("CController - moving a tab keeps the same tab active", "[controller][tabs]")
{
	clearSavedState();
	TempTree tree;
	ControllerSession session;

	for (size_t tabCount = 2; tabCount <= MaxTabCount; ++tabCount)
	{
		addBackgroundTabsUpTo(session, tabCount, tree.path());
		std::vector<qulonglong> expectedIds = session->tabIds(Panel::LeftPanel);

		for (size_t active = 0; active < tabCount; ++active)
		{
			for (size_t from = 0; from < tabCount; ++from)
			{
				for (size_t to = 0; to < tabCount; ++to)
				{
					const qulonglong activeId = expectedIds[active];
					const qulonglong movedId = expectedIds[from];
					session->setActiveTab(Panel::LeftPanel, activeId);
					session->moveTabPosition(Panel::LeftPanel, movedId, to);

					expectedIds.erase(expectedIds.begin() + (ptrdiff_t)from);
					expectedIds.insert(expectedIds.begin() + (ptrdiff_t)to, movedId);

					INFO(tabCount << " tabs, the one at " << active << " active, moved from " << from << " to " << to);
					REQUIRE(session->tabIds(Panel::LeftPanel) == expectedIds);
					REQUIRE(session->activeTabId(Panel::LeftPanel) == activeId);
				}
			}
		}
	}
}

TEST_CASE("CController - closing a tab keeps the active one, and closing the active tab activates the next, or the previous when it was the last", "[controller][tabs]")
{
	clearSavedState();
	TempTree tree;
	ControllerSession session;

	for (size_t tabCount = 2; tabCount <= MaxTabCount; ++tabCount)
	{
		for (size_t active = 0; active < tabCount; ++active)
		{
			for (size_t closed = 0; closed < tabCount; ++closed)
			{
				// The closed tab is replaced at the end of the list, so every case starts from tabCount tabs
				addBackgroundTabsUpTo(session, tabCount, tree.path());
				const std::vector<qulonglong> idsBefore = session->tabIds(Panel::LeftPanel);
				session->setActiveTab(Panel::LeftPanel, idsBefore[active]);
				session->closeTab(Panel::LeftPanel, idsBefore[closed]);

				std::vector<qulonglong> expectedIds = idsBefore;
				expectedIds.erase(expectedIds.begin() + (ptrdiff_t)closed);
				const qulonglong expectedActiveId = closed != active ? idsBefore[active] : expectedIds[std::min(active, expectedIds.size() - 1)];

				INFO(tabCount << " tabs, the one at " << active << " active, the one at " << closed << " closed");
				REQUIRE(session->tabIds(Panel::LeftPanel) == expectedIds);
				REQUIRE(session->activeTabId(Panel::LeftPanel) == expectedActiveId);
			}
		}
	}
}

TEST_CASE("CController - the last tab of a side cannot be closed", "[controller][tabs]")
{
	clearSavedState();
	ControllerSession session;

	const std::vector<qulonglong> ids = session->tabIds(Panel::LeftPanel);
	REQUIRE(ids.size() == 1);
	session->closeTab(Panel::LeftPanel, ids.front());

	CHECK(session->tabIds(Panel::LeftPanel) == ids);
}

TEST_CASE("CController - a new tab takes its sorting from the source tab, or the default without one", "[controller][tabs]")
{
	clearSavedState();
	TempTree tree;
	ControllerSession session;

	const qulonglong sourceId = session->activeTabId(Panel::LeftPanel);
	const CPanel& defaultTab = session->tabById(Panel::LeftPanel, sourceId);
	const FileListSortField defaultField = defaultTab.sortField();
	const Qt::SortOrder defaultOrder = defaultTab.sortOrder();
	const FileListSortField otherField = defaultField == FileListSortField::Size ? FileListSortField::Date : FileListSortField::Size;
	const Qt::SortOrder otherOrder = defaultOrder == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
	session->setTabSorting(Panel::LeftPanel, sourceId, otherField, otherOrder);

	const CPanel& copied = session->tabById(Panel::LeftPanel, session->addTab(Panel::LeftPanel, tree.path(), false, sourceId));
	CHECK(copied.sortField() == otherField);
	CHECK(copied.sortOrder() == otherOrder);

	const CPanel& unsorted = session->tabById(Panel::LeftPanel, session->addTab(Panel::LeftPanel, tree.path(), false));
	CHECK(unsorted.sortField() == defaultField);
	CHECK(unsorted.sortOrder() == defaultOrder);
}

TEST_CASE("CController - a folder's tab is activated, or a new one opens there with the active tab's sorting", "[controller][tabs]")
{
	clearSavedState();
	TempTree tree;
	ControllerSession session;

	const QString existingFolder = tree.makeDir("existing");
	const qulonglong existingId = session->addTab(Panel::LeftPanel, existingFolder, false);
	CHECK(session->activateOrAddTabShowing(Panel::LeftPanel, existingFolder + "/./") == existingId);
	CHECK(session->activeTabId(Panel::LeftPanel) == existingId);
#ifdef _WIN32
	CHECK(session->activateOrAddTabShowing(Panel::LeftPanel, existingFolder.toUpper()) == existingId);
#endif
	CHECK(session->tabCount(Panel::LeftPanel) == 2);

	const CPanel& activeTab = session->tabById(Panel::LeftPanel, existingId);
	const FileListSortField otherField = activeTab.sortField() == FileListSortField::Size ? FileListSortField::Date : FileListSortField::Size;
	const Qt::SortOrder otherOrder = activeTab.sortOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
	session->setTabSorting(Panel::LeftPanel, existingId, otherField, otherOrder);

	const qulonglong newId = session->activateOrAddTabShowing(Panel::LeftPanel, tree.makeDir("new"));
	CHECK(newId != existingId);
	CHECK(session->tabCount(Panel::LeftPanel) == 3);
	CHECK(session->activeTabId(Panel::LeftPanel) == newId);
	CHECK(session->tabById(Panel::LeftPanel, newId).sortField() == otherField);
	CHECK(session->tabById(Panel::LeftPanel, newId).sortOrder() == otherOrder);
	CHECK(session->tabCount(Panel::RightPanel) == 1);
}

TEST_CASE("CController - closed tabs' folders come back most recent first, separately per side", "[controller][tabs]")
{
	clearSavedState();
	TempTree tree;
	ControllerSession session;

	const qulonglong firstId = session->addTab(Panel::LeftPanel, tree.makeDir("first"), false);
	const qulonglong secondId = session->addTab(Panel::LeftPanel, tree.makeDir("second"), false);
	const QString firstPath = session->tabPath(Panel::LeftPanel, firstId);
	const QString secondPath = session->tabPath(Panel::LeftPanel, secondId);
	REQUIRE(firstPath != secondPath);

	session->closeTab(Panel::LeftPanel, firstId);
	session->closeTab(Panel::LeftPanel, secondId);
	session->closeTab(Panel::LeftPanel, session->activeTabId(Panel::LeftPanel)); // The last tab stays open, so nothing is recorded

	CHECK(session->takeLastClosedTabPath(Panel::RightPanel).isEmpty());
	CHECK(session->takeLastClosedTabPath(Panel::LeftPanel) == secondPath);
	CHECK(session->takeLastClosedTabPath(Panel::LeftPanel) == firstPath);
	CHECK(session->takeLastClosedTabPath(Panel::LeftPanel).isEmpty());
}
