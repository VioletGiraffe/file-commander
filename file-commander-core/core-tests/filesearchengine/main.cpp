#define CATCH_CONFIG_RUNNER

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };

#if defined _WIN32 && defined _DEBUG
	// A search pattern that doesn't compile asserts recoverably; the CRT assert must report to stderr instead of
	// opening an interactive dialog.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

	return Catch::Session().run(argc, argv);
}
