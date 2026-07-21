// CEntryPath invariants and the parseOperationPath() table: every supported root/separator form,
// malformed/relative rejection, and the parent/child/name/isRoot laws.

#include "fileoperations/centrypath.h"

#include "fileoperationtesthelpers.h"

// Parses text that must be valid, so the tests below can state expectations on one line.
static CEntryPath parsed(const QString& text)
{
	const auto path = parseOperationPath(text);
	REQUIRE(path.has_value());
	return *path;
}

static void requireParsesTo(const QString& input, const QString& expected)
{
	INFO("input: " << input.toStdString());
	CHECK(parsed(input).value() == expected);
}

static void requireRejected(const QString& input)
{
	INFO("input: " << input.toStdString());
	CHECK(!parseOperationPath(input).has_value());
}

TEST_CASE("parseOperationPath: rejection table", "[entrypath]")
{
	requireRejected({});
	requireRejected(QStringLiteral("   "));
	requireRejected(QStringLiteral("relative/path"));
	requireRejected(QStringLiteral("./x"));
	requireRejected(QStringLiteral("../x"));
	requireRejected(QStringLiteral("file.txt"));

#ifdef _WIN32
	requireRejected(QStringLiteral("C:")); // Drive-relative: names the drive's current directory, not its root
	requireRejected(QStringLiteral("C:foo"));
	requireRejected(QStringLiteral("/rootless")); // Current-drive-relative on Windows
	requireRejected(QStringLiteral("\\rootless"));
	requireRejected(QStringLiteral("1:/x")); // Not a drive letter
	requireRejected(QStringLiteral("\\\\server")); // UNC with no share
	requireRejected(QStringLiteral("\\\\server\\"));
	requireRejected(QStringLiteral("\\\\\\x")); // Three leading separators is no recognized form
	requireRejected(QStringLiteral("\\\\.\\pipe")); // Device namespace
	requireRejected(QStringLiteral("\\\\..\\share"));
#endif
}

#ifdef _WIN32

TEST_CASE("parseOperationPath: Windows drive paths", "[entrypath]")
{
	requireParsesTo(QStringLiteral("C:\\foo\\bar.txt"), QStringLiteral("C:/foo/bar.txt"));
	requireParsesTo(QStringLiteral("C:/foo/bar.txt"), QStringLiteral("C:/foo/bar.txt"));
	requireParsesTo(QStringLiteral("c:/foo"), QStringLiteral("C:/foo")); // Drive letter is canonically uppercase
	requireParsesTo(QStringLiteral("C:/"), QStringLiteral("C:/"));
	requireParsesTo(QStringLiteral("C:\\"), QStringLiteral("C:/"));
	requireParsesTo(QStringLiteral("C:/foo/"), QStringLiteral("C:/foo")); // No trailing separator except a root
	requireParsesTo(QStringLiteral("C://foo\\\\bar"), QStringLiteral("C:/foo/bar")); // Duplicate separators collapse
	requireParsesTo(QStringLiteral("C:/a/./b"), QStringLiteral("C:/a/b"));
	requireParsesTo(QStringLiteral("C:/a/b/../c"), QStringLiteral("C:/a/c"));
	requireParsesTo(QStringLiteral("C:/../../x"), QStringLiteral("C:/x")); // ".." clamps at the root
	requireParsesTo(QStringLiteral("C:/a/.."), QStringLiteral("C:/"));
	requireParsesTo(QStringLiteral("  C:/foo  "), QStringLiteral("C:/foo")); // Typed-text whitespace artifact
	requireParsesTo(QStringLiteral("C:\\mixed/separators\\path"), QStringLiteral("C:/mixed/separators/path"));
}

TEST_CASE("parseOperationPath: Windows UNC paths", "[entrypath]")
{
	requireParsesTo(QStringLiteral("\\\\server\\share"), QStringLiteral("//server/share"));
	requireParsesTo(QStringLiteral("\\\\server\\share\\"), QStringLiteral("//server/share"));
	requireParsesTo(QStringLiteral("\\\\server\\share\\dir\\file"), QStringLiteral("//server/share/dir/file"));
	requireParsesTo(QStringLiteral("//server/share/dir"), QStringLiteral("//server/share/dir"));
	requireParsesTo(QStringLiteral("\\\\server\\share\\a\\..\\b"), QStringLiteral("//server/share/b"));
	requireParsesTo(QStringLiteral("\\\\server\\share\\.."), QStringLiteral("//server/share")); // Clamps at the share root
	requireParsesTo(QStringLiteral("\\\\server\\share\\\\x"), QStringLiteral("//server/share/x"));
}

TEST_CASE("CEntryPath: Windows root and hierarchy laws", "[entrypath]")
{
	const CEntryPath driveRoot = parsed(QStringLiteral("C:/"));
	CHECK(driveRoot.isRoot());
	CHECK(driveRoot.name() == QStringLiteral("C:/"));

	const CEntryPath file = parsed(QStringLiteral("C:/a/b.txt"));
	CHECK(!file.isRoot());
	CHECK(file.name() == QStringLiteral("b.txt"));
	CHECK(file.parent().value() == QStringLiteral("C:/a"));
	CHECK(file.parent().parent().value() == QStringLiteral("C:/"));
	CHECK(file.parent().parent().isRoot());
	CHECK(file.parent().child(file.name()) == file);
	CHECK(driveRoot.child(QStringLiteral("x")).value() == QStringLiteral("C:/x"));
	CHECK(driveRoot.child(QStringLiteral("x")).parent() == driveRoot);

	const CEntryPath uncRoot = parsed(QStringLiteral("\\\\server\\share"));
	CHECK(uncRoot.isRoot());
	CHECK(uncRoot.name() == QStringLiteral("//server/share"));
	const CEntryPath uncFile = parsed(QStringLiteral("\\\\server\\share\\a\\b"));
	CHECK(!uncFile.isRoot());
	CHECK(uncFile.name() == QStringLiteral("b"));
	CHECK(uncFile.parent().value() == QStringLiteral("//server/share/a"));
	CHECK(uncFile.parent().parent() == uncRoot);
	CHECK(uncRoot.child(QStringLiteral("x")).value() == QStringLiteral("//server/share/x"));
	CHECK(uncRoot.child(QStringLiteral("x")).parent() == uncRoot);
}

TEST_CASE("CEntryPath: case-insensitive spelling comparison", "[entrypath]")
{
	CHECK(parsed(QStringLiteral("C:/Foo/Bar")) == parsed(QStringLiteral("C:/foo/bar")));
	CHECK(!(parsed(QStringLiteral("C:/foo")) == parsed(QStringLiteral("C:/bar"))));
}

#else

TEST_CASE("parseOperationPath: POSIX paths", "[entrypath]")
{
	requireParsesTo(QStringLiteral("/"), QStringLiteral("/"));
	requireParsesTo(QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/local/bin"));
	requireParsesTo(QStringLiteral("/usr/"), QStringLiteral("/usr")); // No trailing separator except the root
	requireParsesTo(QStringLiteral("//usr///bin"), QStringLiteral("/usr/bin")); // Duplicate separators collapse
	requireParsesTo(QStringLiteral("/a/./b"), QStringLiteral("/a/b"));
	requireParsesTo(QStringLiteral("/a/b/../c"), QStringLiteral("/a/c"));
	requireParsesTo(QStringLiteral("/../x"), QStringLiteral("/x")); // ".." clamps at the root, as the OS resolves it
	requireParsesTo(QStringLiteral("/a/.."), QStringLiteral("/"));
	requireParsesTo(QStringLiteral("  /tmp/x  "), QStringLiteral("/tmp/x")); // Typed-text whitespace artifact
	requireParsesTo(QStringLiteral("/name.with\\backslash"), QStringLiteral("/name.with\\backslash")); // '\' is an ordinary name character
}

TEST_CASE("CEntryPath: POSIX root and hierarchy laws", "[entrypath]")
{
	const CEntryPath root = parsed(QStringLiteral("/"));
	CHECK(root.isRoot());
	CHECK(root.name() == QStringLiteral("/"));

	const CEntryPath file = parsed(QStringLiteral("/a/b.txt"));
	CHECK(!file.isRoot());
	CHECK(file.name() == QStringLiteral("b.txt"));
	CHECK(file.parent().value() == QStringLiteral("/a"));
	CHECK(file.parent().parent().value() == QStringLiteral("/"));
	CHECK(file.parent().parent().isRoot());
	CHECK(file.parent().child(file.name()) == file);
	CHECK(root.child(QStringLiteral("x")).value() == QStringLiteral("/x"));
	CHECK(root.child(QStringLiteral("x")).parent() == root);
}

TEST_CASE("CEntryPath: spelling comparison follows platform case policy", "[entrypath]")
{
#ifdef __APPLE__
	CHECK(parsed(QStringLiteral("/Foo/Bar")) == parsed(QStringLiteral("/foo/bar")));
#else
	CHECK(!(parsed(QStringLiteral("/Foo/Bar")) == parsed(QStringLiteral("/foo/bar"))));
#endif
	CHECK(!(parsed(QStringLiteral("/foo")) == parsed(QStringLiteral("/bar"))));
}

#endif

TEST_CASE("parseOperationPath: parse round trip is the identity", "[entrypath]")
{
#ifdef _WIN32
	const QString inputs[] { QStringLiteral("C:/"), QStringLiteral("C:/a/b c/d.txt"), QStringLiteral("//server/share"), QStringLiteral("//server/share/x") };
#else
	const QString inputs[] { QStringLiteral("/"), QStringLiteral("/a/b c/d.txt"), QStringLiteral("/tmp") };
#endif
	for (const auto& input : inputs)
	{
		const CEntryPath path = parsed(input);
		CHECK(parsed(path.value()).value() == path.value());
	}
}
