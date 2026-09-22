// Submodule includes
#include "compiler/compiler_warnings_control.h"


#define CATCH_CONFIG_RUNNER
DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	// A headless caller selects the offscreen platform through QT_QPA_PLATFORM (the CI Linux run line does).
	QApplication app{ argc, argv };

	return Catch::Session().run(argc, argv);
}
