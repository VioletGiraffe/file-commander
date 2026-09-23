#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp" // First: compiles catch.hpp with the runner


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

#include <random>
#include <stdint.h>

uint32_t g_randomSeed = std::random_device{}();

int main(int argc, char* argv[])
{
	// A headless caller selects the offscreen platform through QT_QPA_PLATFORM (the CI Linux run line does).
	QApplication app{ argc, argv };

	Catch::Session session;
	session.cli(session.cli() | Catch::clara::Opt(g_randomSeed, "std::random seed")["--std-seed"]("std::random seed"));
	return runCatchSession(session, argc, argv);
}
