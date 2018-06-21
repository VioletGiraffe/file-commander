#include "cfilesystemobject.h"
#include "utility/macro_utils.h"

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

TEST_CASE("Empty CFileSystemObject test", "[CFileSystemObject]")
{
	CFileSystemObject fso((QFileInfo_Test()));

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso == CFileSystemObject());
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(!fso.exists());
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.extension() == "");
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.fullAbsolutePath() == "");
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.fullName() == "");
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.hash() == 0);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isCdUp() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isChildOf(CFileSystemObject()) == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isDir() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isEmptyDir() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isExecutable() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isFile() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isHidden() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isMovableTo(CFileSystemObject()) == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isNetworkObject() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isReadable() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isValid() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.isWriteable() == false);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.name() == "");
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.parentDirPath() == "");
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.size() == 0);
	}

	SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__)) {
		CHECK(fso.type() == UnknownType);
	}
}
