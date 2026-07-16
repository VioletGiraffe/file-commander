#include "cfilesystemobject.h"

#include <QTemporaryDir>

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

TEST_CASE("isMovableTo for a non-existent destination path", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());

	const CFileSystemObject existingDir{ tempDir.path() };
	REQUIRE(existingDir.exists());

	// A destination that doesn't exist yet, nested inside the existing temp dir (e.g. a move target folder still to be created)
	const CFileSystemObject nonExistentChild{ tempDir.path() + "/does_not_exist/child.txt" };
	REQUIRE(!nonExistentChild.exists());

	// Regression: rootFileSystemId() used to read uninitialized memory for a non-existent path and return a garbage device ID.
	// It must resolve to the device of the nearest existing ancestor, so moving into a not-yet-created subfolder of the same
	// volume is recognized as a same-drive move rather than a cross-device copy.
	CHECK(existingDir.rootFileSystemId() == nonExistentChild.rootFileSystemId());
	CHECK(existingDir.isMovableTo(nonExistentChild));
}

