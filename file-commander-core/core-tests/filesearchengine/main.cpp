// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };
	return runCatchSession(argc, argv);
}
