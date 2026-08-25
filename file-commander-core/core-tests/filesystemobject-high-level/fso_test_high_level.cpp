#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h" // toNativeSeparators
#include "link_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#define CATCH_CONFIG_MAIN
#include "3rdparty/catch2/catch.hpp"

TEST_CASE("::pathHierarchy tests", "[CFileSystemObject]")
{
	CHECK(::pathHierarchy( {} ).empty());

	{
		const auto hierarchy = ::pathHierarchy(".");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "."));
	}

	{
		const auto hierarchy = ::pathHierarchy("..");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == ".."));
	}

	{
		const auto hierarchy = ::pathHierarchy("/");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "/"));
	}

#ifndef _WIN32
	{
		const auto hierarchy = ::pathHierarchy("/Users/admin/Downloads/1.txt");
		const std::vector<QString> reference{"/Users/admin/Downloads/1.txt", "/Users/admin/Downloads/", "/Users/admin/", "/Users/", "/"};
		CHECK(hierarchy == reference);
	}
#else
	{
		const auto hierarchy = ::pathHierarchy("R:/Docs/1/2/3/txt.files/important.document.txt");
		const std::vector<QString> reference{ "R:/Docs/1/2/3/txt.files/important.document.txt", "R:/Docs/1/2/3/txt.files/", "R:/Docs/1/2/3/", "R:/Docs/1/2/", "R:/Docs/1/", "R:/Docs/", "R:/"};
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = ::pathHierarchy("R:");
		const std::vector<QString> reference{ "R:" };
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = ::pathHierarchy("R:/");
		const std::vector<QString> reference{ "R:/" };
		CHECK(hierarchy == reference);
	}
#endif
}

TEST_CASE("rootFileSystemId for a non-existent path", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());

	const CFileSystemObject existingDir{ tempDir.path() };
	REQUIRE(existingDir.exists());

	const CFileSystemObject nonExistentChild{ tempDir.path() + "/does_not_exist/child.txt" };
	REQUIRE(!nonExistentChild.exists());

	// Regression: rootFileSystemId() used to read uninitialized memory for a non-existent path and return a garbage
	// device ID. It must resolve to the device of the nearest existing ancestor.
	CHECK(existingDir.rootFileSystemId() == nonExistentChild.rootFileSystemId());
}

// The trailing separator on a directory's path is load-bearing: it is what makes "dir" and "dir/" hash and compare
// equal, it is how a non-existent path gets classified at all, and parentDirPath() preserves it.
TEST_CASE("A directory's path always ends with a separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/folder";
	REQUIRE(QDir{}.mkpath(folder));

	const CFileSystemObject withoutSeparator{ folder };
	const CFileSystemObject withSeparator{ folder + "/" };

	REQUIRE(withoutSeparator.isDir());
	CHECK(withoutSeparator.fullAbsolutePath() == folder + "/");
	CHECK(withSeparator.fullAbsolutePath() == folder + "/");

	// The point of the normalization: either spelling names the same object.
	CHECK(withoutSeparator.hash() == withSeparator.hash());
	CHECK(withoutSeparator == withSeparator);

	// The native spelling round-trips, which is what lets a path leave through currentDirPathNative() and come back.
	CHECK(CFileSystemObject{ toNativeSeparators(withoutSeparator.fullAbsolutePath()) } == withoutSeparator);

	const QString filePath = folder + "/file.txt";
	{
		QFile file{ filePath };
		REQUIRE(file.open(QFile::WriteOnly));
	}

	const CFileSystemObject fileObject{ filePath };
	REQUIRE(fileObject.isFile());
	CHECK(fileObject.fullAbsolutePath() == filePath); // Only directories carry the separator
	CHECK(fileObject.parentDirPath() == folder + "/");
}

// Nothing on disk to inspect means the spelling is the only classification available. This is what lets a copy or move
// destination be recognized as a folder before it exists.
TEST_CASE("A non-existent path is a directory exactly when spelled with a separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString missing = tempDir.path() + "/not_created";

	const CFileSystemObject asDirectory{ missing + "/" };
	REQUIRE(!asDirectory.exists());
	CHECK(asDirectory.isDir());
	CHECK(asDirectory.type() == Directory);
	CHECK(asDirectory.fullAbsolutePath() == missing + "/");

	const CFileSystemObject asEntry{ missing };
	REQUIRE(!asEntry.exists());
	CHECK(!asEntry.isDir());
	CHECK(!asEntry.isFile());
	CHECK(asEntry.fullAbsolutePath() == missing);
}

// Duplicate separators and "."/".." resolve before the trailing separator is applied, so a directory still comes out
// with exactly one.
TEST_CASE("Path normalization precedes the trailing separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/folder";
	REQUIRE(QDir{}.mkpath(folder));

	CHECK(CFileSystemObject{ folder + "//" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ tempDir.path() + "//folder" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ folder + "/." }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ folder + "/../folder" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ QDir::toNativeSeparators(folder) }.fullAbsolutePath() == folder + "/");
}

// Qt reports a dotted directory name as though its last component were an extension, so the name is reassembled.
TEST_CASE("A dotted directory name is reported whole", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/txt.files";
	REQUIRE(QDir{}.mkpath(folder));

	for (const CFileSystemObject& object : { CFileSystemObject{ folder }, CFileSystemObject{ folder + "/" } })
	{
		CHECK(object.fullName() == "txt.files");
		CHECK(object.extension().isEmpty()); // A directory has no extension, however its name is spelled
	}
}

// Qt's isDir() follows a directory link and cannot be told not to, so a live link is classified - and
// separator-normalized - as the directory it points at. A dead one stays a listed, deletable entry either way, but its
// classification diverges: a Windows junction carries the directory attribute on the link entry itself, so it remains a
// Directory with nothing left to point at, while on POSIX there is nothing to follow and it falls back to File.
TEST_CASE("A directory link is a directory until its target is gone", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString target = tempDir.path() + "/target";
	const QString link = tempDir.path() + "/link";
	REQUIRE(QDir{}.mkpath(target));
	REQUIRE(createDirectoryLink(target, link));

	{
		const CFileSystemObject linkObject{ link };
		REQUIRE(linkObject.exists());
		CHECK(linkObject.isLink());
		CHECK(linkObject.isDir());
		CHECK(linkObject.type() == Directory);
		CHECK(linkObject.fullAbsolutePath() == link + "/"); // Followed, so it is separator-normalized like any directory
		CHECK(CFileSystemObject{ link + "/" } == linkObject);
	}

	REQUIRE(QDir{ target }.removeRecursively());

	const CFileSystemObject brokenLink{ link };
	CHECK(brokenLink.isLink());
	CHECK(brokenLink.exists()); // A link entry exists even when its target does not

#ifdef _WIN32
	CHECK(brokenLink.isDir());
	CHECK(brokenLink.type() == Directory);
	CHECK(brokenLink.fullAbsolutePath() == link + "/");
#else
	CHECK(!brokenLink.isDir());
	CHECK(brokenLink.type() == File);
	CHECK(brokenLink.fullAbsolutePath() == link);
#endif
}

TEST_CASE("A filesystem root is its own path and has no parent", "[CFileSystemObject]")
{
	const CFileSystemObject root{ QDir::rootPath() };
	REQUIRE(root.isDir());
	CHECK(root.fullAbsolutePath().endsWith('/'));
	CHECK(root.parentDirPath().isEmpty()); // What stops navigateUp() at the top of a volume

#ifdef _WIN32
	// The drive letter is canonically uppercase, so either spelling hashes to the same object.
	CHECK(CFileSystemObject{ QStringLiteral("c:/") } == CFileSystemObject{ QStringLiteral("C:/") });
#endif
}

