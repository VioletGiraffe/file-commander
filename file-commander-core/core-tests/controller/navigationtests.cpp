#include "controllertesthelpers.h"

#include <vector>

// Not the controller's own key: these tests use instances of their own
static const QString TestFavoritesKey = QStringLiteral("Test/Favorites");

TEST_CASE("CFavoriteLocations - an item is refused an empty name, and a name or a location another item of its list has", "[controller][favorites]")
{
	clearSavedState();
	CFavoriteLocations favorites{ TestFavoritesKey };
	std::vector<CLocationsCollection>& root = favorites.locations();

	REQUIRE_FALSE(favorites.addLocation(root, "a", "/x"));
	CHECK(favorites.addLocation(root, "b", "/x"));
	CHECK(favorites.addLocation(root, "a", "/y"));
	CHECK(favorites.addCategory(root, "a"));
	CHECK(favorites.addLocation(root, {}, "/y"));
	CHECK(favorites.addLocation(root, "d", {}));
	REQUIRE_FALSE(favorites.addCategory(root, "c"));
	CHECK_FALSE(favorites.addLocation(root[1].subLocations, "a", "/x")); // Another list
	REQUIRE(root.size() == 2);

	CHECK_FALSE(favorites.editItem(root, 0, "a", "/z")); // The item's own name
	CHECK(favorites.editItem(root, 0, "c", "/z")); // The category's name
	CHECK(favorites.editItem(root, 0, "a", {}));
	CHECK(root[0].absolutePath == "/z");

	favorites.removeItem(root, 1);
	REQUIRE(root.size() == 1);
	CHECK(root[0].displayName == "a");
}

TEST_CASE("CFavoriteLocations - the locations and categories are saved under the key given", "[controller][favorites]")
{
	clearSavedState();
	{
		CFavoriteLocations favorites{ TestFavoritesKey };
		REQUIRE_FALSE(favorites.addCategory(favorites.locations(), "category"));
		REQUIRE_FALSE(favorites.addLocation(favorites.locations()[0].subLocations, "inner", "/inner"));
		REQUIRE_FALSE(favorites.addLocation(favorites.locations(), "outer", "/outer"));
	}
	CHECK_FALSE(QSettings{}.contains(KEY_FAVORITES));

	CFavoriteLocations favorites{ TestFavoritesKey };
	const std::vector<CLocationsCollection>& root = favorites.locations();
	REQUIRE(root.size() == 2);
	CHECK(root[0].isCategory());
	REQUIRE(root[0].subLocations.size() == 1);
	CHECK(root[0].subLocations[0].absolutePath == "/inner");
	CHECK(root[1].absolutePath == "/outer");
}

TEST_CASE("CController - the recent folders are this side's, then those visited only on the other side, each the most recent first", "[controller][history]")
{
	clearSavedState();
	TempTree tree;
	const QString a = tree.makeDir("a"), b = tree.makeDir("b"), c = tree.makeDir("c");
	ControllerSession session;

	// The folder the panel shows, as the log records it
	const auto visit = [&session](Panel p, const QString& folder) {
		REQUIRE(session->setPath(p, folder, refreshCauseOther) == FileOperationResultCode::Ok);
		return session->panel(p).currentDirPathPosix();
	};
	const QString leftStart = session->panel(Panel::LeftPanel).currentDirPathPosix();
	const QString rightStart = session->panel(Panel::RightPanel).currentDirPathPosix();
	const QString leftA = visit(Panel::LeftPanel, a), leftB = visit(Panel::LeftPanel, b);
	const QString rightC = visit(Panel::RightPanel, c);
	visit(Panel::RightPanel, a);

	std::vector<QString> expected{ leftB, leftA, leftStart, rightC };
	if (rightStart != leftStart)
		expected.push_back(rightStart);
	CHECK(session->recentFolders(Panel::LeftPanel) == expected);
}
