#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp" // First: compiles catch.hpp with the runner


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };
	return runCatchSession(argc, argv);
}
