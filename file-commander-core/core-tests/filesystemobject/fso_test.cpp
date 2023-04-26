#include "cfilesystemobject.h"
#include "../test-utils/src/catch2_utils.hpp"

#define CATCH_CONFIG_MAIN
#include "3rdparty/catch2/catch.hpp"

TEST_CASE("Empty CFileSystemObject test", "[CFileSystemObject]")
{
	CFileSystemObject fso{ QFileInfo_Test{} };

	SECTION_WITH_AUTO_NAME {
		CHECK(fso == CFileSystemObject{});
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(!fso.exists());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.extension().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.fullAbsolutePath().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.fullName().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.hash() == 0);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isCdUp() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isDir() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isEmptyDir() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isExecutable() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isFile() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isHidden() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isMovableTo(CFileSystemObject{}) == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isNetworkObject() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isReadable() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isValid() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isWriteable() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.name().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.parentDirPath().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.size() == 0);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.type() == UnknownType);
	}
}
