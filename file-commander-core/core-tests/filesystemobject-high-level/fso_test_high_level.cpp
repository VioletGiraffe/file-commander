#include "cfilesystemobject.h"

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

TEST_CASE("CFileSystemObject::pathHierarchy tests", "[CFileSystemObject]" )
{
	CHECK(CFileSystemObject::pathHierarchy("").empty());

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy(".");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "."));
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("..");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == ".."));
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("/");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "/"));
	}

#ifndef _WIN32
	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("/Users/admin/Downloads/1.txt");
		const std::vector<QString> reference{"/Users/admin/Downloads/1.txt", "/Users/admin/Downloads", "/Users/admin", "/Users", "/"};
		CHECK(hierarchy == reference);
	}
#else
	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:/Docs/1/2/3/txt.files/important.document.txt");
		const std::vector<QString> reference{ "R:/Docs/1/2/3/txt.files/important.document.txt", "R:/Docs/1/2/3/txt.files", "R:/Docs/1/2/3", "R:/Docs/1/2", "R:/Docs/1", "R:/Docs", "R:/"};
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:");
		const std::vector<QString> reference{ "R:" };
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:/");
		const std::vector<QString> reference{ "R:/" };
		CHECK(hierarchy == reference);
	}
#endif
}

