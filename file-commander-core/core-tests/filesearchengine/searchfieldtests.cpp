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
