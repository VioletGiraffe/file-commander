#include "searchenginetesthelpers.h"

#include "filesystemhelperfunctions.h"

TEST_CASE("Search fields - the name field's filters are split at ';' and kept as typed with partial match", "[search][fields]")
{
	CHECK(nameFiltersFromSearchField(QSL("notes;*.txt;"), true) == QStringList{ QSL("notes"), QSL("*.txt") });
	CHECK(nameFiltersFromSearchField({}, true).isEmpty());
}

TEST_CASE("Search fields - without partial match a name filter matches the whole name", "[search][fields]")
{
	TempTree tree;
	const QString exact = tree.makeFile(QSL("notes.txt"));
	const QString longer = tree.makeFile(QSL("my notes.txt.bak"));
	const QString withCaret = tree.makeFile(QSL("^notes.txt"));

	const auto search = [&tree](const QString& fieldText, const bool partialMatch) {
		return runSearch({ .roots = { tree.path() }, .nameFilters = nameFiltersFromSearchField(fieldText, partialMatch) });
	};

	SECTION("With it, the filter matches anywhere in the name")
	{
		const SearchResult result = search(QSL("notes.txt"), true);
		CHECK(result.matched(exact));
		CHECK(result.matched(longer));
		CHECK(result.matched(withCaret));
	}

	SECTION("Without it, only the whole name")
	{
		const SearchResult result = search(QSL("notes.txt"), false);
		CHECK(result.matched(exact));
		CHECK(result.count() == 1);
	}

	SECTION("Without it, a typed '^' is a literal character")
	{
		const SearchResult result = search(QSL("^notes.txt"), false);
		CHECK(result.matched(withCaret));
		CHECK(result.count() == 1);
	}

	SECTION("Without it, the wildcards still work")
	{
		const SearchResult result = search(QSL("*.txt"), false);
		CHECK(result.matched(exact));
		CHECK(result.matched(withCaret));
		CHECK(result.count() == 2);
	}

	SECTION("'*' matches every name either way")
	{
		CHECK(nameFiltersFromSearchField(QSL("*"), false) == QStringList{ QSL("*") });
		CHECK(search(QSL("*"), false).count() == 3);
	}
}

TEST_CASE("Search fields - an excluded folder is skipped with its contents, by its whole name ignoring case", "[search][fields]")
{
	TempTree tree;
	const QString git = tree.makeDir(QSL(".git"));
	const QString inGit = tree.makeFile(QSL(".git/a.txt"));
	const QString underBuild = tree.makeDir(QSL("Build/deep"));
	const QString inBuild = tree.makeFile(QSL("Build/deep/a.txt"));
	const QString github = tree.makeDir(QSL(".github")); // Contains an excluded name, but is not one
	const QString inGithub = tree.makeFile(QSL(".github/a.txt"));
	const QString fileWithExcludedName = tree.makeFile(QSL("node_modules"));
	const QString outside = tree.makeFile(QSL("a.txt"));

	const QStringList excludedFolders = excludedFolderFiltersFromSearchField(QSL(".git;build;node_m*"));
	const SearchResult result = runSearch({ .roots = { tree.path() }, .excludedFolders = excludedFolders });
	CHECK_FALSE(result.matched(git));
	CHECK_FALSE(result.matched(inGit));
	CHECK_FALSE(result.matched(tree.path(QSL("Build"))));
	CHECK_FALSE(result.matched(underBuild));
	CHECK_FALSE(result.matched(inBuild));
	CHECK(result.matched(github));
	CHECK(result.matched(inGithub));
	CHECK(result.matched(fileWithExcludedName));
	CHECK(result.matched(outside));
	CHECK(result.count() == 4);

	// A root is searched even when its name matches
	CHECK(runSearch({ .roots = { git }, .excludedFolders = excludedFolders }).matched(inGit));
	CHECK(excludedFolderFiltersFromSearchField({}).isEmpty());
}

TEST_CASE("Search fields - the \"Search in\" field's paths are separated by \"; \" and resolved against the base folder", "[search][fields]")
{
	const TempTree tree;
	const QString base = tree.path();
	const QString sub = tree.path(QSL("sub"));
	const QString other = tree.path(QSL("other"));

	CHECK(rootsFromSearchInField(QSL("sub; ") + toNativeSeparators(other), base) == QStringList{ sub, other });
	CHECK(rootsFromSearchInField(QSL("sub; "), base) == QStringList{ sub });
	CHECK(rootsFromSearchInField(QSL("a;b"), base) == QStringList{ tree.path(QSL("a;b")) }); // ';' alone belongs to the name
	CHECK(rootsFromSearchInField({}, base).isEmpty());
}

TEST_CASE("Search fields - only a \"Search in\" text of one path belongs in the field's history", "[search][fields]")
{
	CHECK(searchInFieldTextBelongsInHistory(QSL("/a/b")));
	CHECK(searchInFieldTextBelongsInHistory(QSL("/a;b")));
	CHECK_FALSE(searchInFieldTextBelongsInHistory(QSL("/a; /b")));
}
