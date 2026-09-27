// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
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

	return runCatchSession(argc, argv, { .randomSeed = &g_randomSeed });
}
