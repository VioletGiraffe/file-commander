#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

// test_utils
#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"


// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
#include "compiler/compiler_warnings_control.h"
#include "lang/type_traits_fast.hpp"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils
RESTORE_COMPILER_WARNINGS

#include <random>

uint32_t g_randomSeed = []{
	std::random_device rd;
	return std::uniform_int_distribution<uint32_t>{0, uint32_max}(rd);
}();

int main(int argc, char* argv[])
{
	// A hook violation is a test-logic error; make it fail the test that caused it rather than only logging to stderr.
	OperationTestHooks::CFaultHookScope::setViolationReporter([](const std::string& message) {
		FAIL_CHECK("Operation test hook violation: " << message);
	});

	return runCatchSession(argc, argv, { .randomSeed = &g_randomSeed, .beforeRun = [] {
		CRandomDataGenerator randomGenerator;
		randomGenerator.setSeed(g_randomSeed);
		Logger() << "RNG consistency check: seed = " << g_randomSeed << ", first RN = " << randomGenerator.randomNumber<uint32_t>(0u, uint32_max);
	} });
}
