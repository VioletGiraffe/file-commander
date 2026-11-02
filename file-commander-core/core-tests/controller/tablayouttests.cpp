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
