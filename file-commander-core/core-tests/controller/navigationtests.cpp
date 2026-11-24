#include "controllertesthelpers.h"

#include <vector>

// Not the controller's own keys: these tests use instances of their own
static const QString TestFavoritesKey = QStringLiteral("Test/Favorites");
static const QString TestLegacyFavoritesKey = QStringLiteral("Test/LegacyFavorites");

TEST_CASE("CFavoriteLocations - a location is refused without a path and a group without a name; a location without a name is named after its folder", "[controller][favorites]")
{
	clearSavedState();
	CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
	std::vector<CLocationsCollection>& root = favorites.locations();

	CHECK(favorites.addLocation(root, "a", {}));
	CHECK(favorites.addGroup(root, {}));
	REQUIRE(root.empty());

	REQUIRE_FALSE(favorites.addLocation(root, "a", "/x"));
	REQUIRE_FALSE(favorites.addLocation(root, "a", "/x")); // The same name and path again
	REQUIRE_FALSE(favorites.addLocation(root, {}, "/x/folder/"));
	REQUIRE_FALSE(favorites.addLocation(root, {}, "/"));
	REQUIRE_FALSE(favorites.addGroup(root, "a"));
	REQUIRE(root.size() == 5);
	CHECK(root[2].displayName == "folder");
	CHECK(root[3].displayName == "/");
	CHECK(root[4].isGroup());

	CHECK(favorites.editItem(root, 0, "b", {}));
	CHECK(favorites.editItem(root, 4, {}, {}));
	REQUIRE_FALSE(favorites.editItem(root, 0, {}, "/z/y"));
	CHECK(root[0].displayName == "y");
	CHECK(root[0].path == "/z/y");

	favorites.removeItem(root, 1);
	REQUIRE(root.size() == 4);
	CHECK(root[1].displayName == "folder");
}

TEST_CASE("CFavoriteLocations - the locations, groups and separators are saved under the key given", "[controller][favorites]")
{
	clearSavedState();
	{
		CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
		REQUIRE_FALSE(favorites.addGroup(favorites.locations(), "group"));
		REQUIRE_FALSE(favorites.addLocation(favorites.locations()[0].subLocations, "inner", "/inner"));

		std::vector<CLocationsCollection> items = favorites.locations();
		items[0].subLocations.push_back(CLocationsCollection::separator());
		items.push_back(CLocationsCollection::separator());
		items.push_back(CLocationsCollection::location("outer", "/outer"));
		favorites.setLocations(std::move(items));
	}
	CHECK_FALSE(QSettings{}.contains(KEY_FAVORITES));
	CHECK_FALSE(QSettings{}.contains(TestLegacyFavoritesKey));

	CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
	const std::vector<CLocationsCollection>& root = favorites.locations();
	REQUIRE(root.size() == 3);
	CHECK(root[0].isGroup());
	CHECK(root[0].displayName == "group");
	REQUIRE(root[0].subLocations.size() == 2);
	CHECK(root[0].subLocations[0].displayName == "inner");
	CHECK(root[0].subLocations[0].path == "/inner");
	CHECK(root[0].subLocations[1].isSeparator());
	CHECK(root[1].isSeparator());
	CHECK(root[2].displayName == "outer");
	CHECK(root[2].path == "/outer");
}

TEST_CASE("CFavoriteLocations - groups nested several levels deep keep the items after them on their own level", "[controller][favorites]")
{
	clearSavedState();
	for (const int depth : { 2, 3 })
	{
		INFO("Groups nested " << depth << " deep");
		{
			CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
			favorites.locations().clear();
			std::vector<CLocationsCollection>* list = &favorites.locations();
			for (int level = 0; level < depth; ++level)
			{
				REQUIRE_FALSE(favorites.addGroup(*list, QStringLiteral("group %1").arg(level)));
				list = &list->back().subLocations;
			}
			REQUIRE_FALSE(favorites.addLocation(*list, "innermost", "/innermost"));
			REQUIRE_FALSE(favorites.addLocation(favorites.locations(), "after", "/after"));
		}

		CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
		const std::vector<CLocationsCollection>& root = favorites.locations();
		REQUIRE(root.size() == 2);
		CHECK(root[1].path == "/after");
		const std::vector<CLocationsCollection>* list = &root;
		for (int level = 0; level < depth; ++level)
		{
			REQUIRE(list->size() == (level == 0 ? 2u : 1u));
			list = &list->front().subLocations;
		}
		REQUIRE(list->size() == 1);
		CHECK(list->front().path == "/innermost");
	}
}

TEST_CASE("CFavoriteLocations - favorites saved by earlier versions load as they were, are saved under the new key, and stay under the old one", "[controller][favorites]")
{
	clearSavedState();
	QByteArray data;
	const auto appendInt = [&data](const int value) { data.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
	const auto appendItem = [&](const QByteArray& name, const QByteArray& path, const int marker) {
		appendInt((int)name.size());
		data.append(name);
		appendInt((int)path.size());
		data.append(path);
		appendInt(marker);
	};
	// NoMarker 0, NextLevel 1, LevelEnded 2
	appendItem("group", {}, 1);
	appendItem("inner", "/inner", 2);
	appendItem("outer", "/outer", 0);
	QSettings{}.setValue(TestLegacyFavoritesKey, data);

	const auto checkItems = [](const std::vector<CLocationsCollection>& root) {
		REQUIRE(root.size() == 2);
		CHECK(root[0].isGroup());
		REQUIRE(root[0].subLocations.size() == 1);
		CHECK(root[0].subLocations[0].path == "/inner");
		CHECK(root[1].path == "/outer");
	};

	{
		CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
		checkItems(favorites.locations());
	}
	CHECK(QSettings{}.value(TestLegacyFavoritesKey).toByteArray() == data);

	// From the new key now: the old one is no longer read
	QSettings{}.setValue(TestLegacyFavoritesKey, QByteArray{});
	CFavoriteLocations favorites{ TestFavoritesKey, TestLegacyFavoritesKey };
	checkItems(favorites.locations());
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
