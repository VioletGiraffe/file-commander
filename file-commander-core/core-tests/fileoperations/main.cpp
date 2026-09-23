#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp" // First: compiles catch.hpp with the runner

#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

// test_utils
#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"


// Submodule includes
#include "lang/type_traits_fast.hpp"


#include <random>

uint32_t g_randomSeed = []{
	std::random_device rd;
	return std::uniform_int_distribution<uint32_t>{0, uint32_max}(rd);
}();

int main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

	// Build a new parser on top of Catch's
	using namespace Catch::clara;
	auto cli
		= session.cli() // Get Catch's composite command line parser
		| Opt(g_randomSeed, "std::random seed") // bind variable to a new option, with a hint string
		["--std-seed"]        // the option names it will respond to
		("std::random seed"); // description string for the help output

	// Now pass the new composite back to Catch so it uses that
	session.cli(cli);

	// A hook violation is a test-logic error; make it fail the test that caused it rather than only logging to stderr.
	OperationTestHooks::CFaultHookScope::setViolationReporter([](const std::string& message) {
		FAIL_CHECK("Operation test hook violation: " << message);
	});

	return runCatchSession(session, argc, argv, [] {
		CRandomDataGenerator randomGenerator;
		randomGenerator.setSeed(g_randomSeed);
		Logger() << "RNG consistency check: seed = " << g_randomSeed << ", first RN = " << randomGenerator.randomNumber<uint32_t>(0u, uint32_max);
	});
}
