#define CATCH_CONFIG_RUNNER

#include "fileoperationtesthelpers.h"

#include "fileoperations/operationtesthooks.h"

// test_utils
#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"

#include "lang/type_traits_fast.hpp"

#include <random>
#include <stdlib.h>

#ifdef _WIN32
#include <crtdbg.h>
#endif

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

	// Let Catch (using Clara) parse the command line
	const int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	srand(g_randomSeed);

#if defined _WIN32 && defined _DEBUG
	// Some tests deliberately exercise recoverable-assert failure paths (e.g. submitDecision rejecting an
	// illegal action); the CRT assert must report to stderr instead of opening an interactive dialog.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

	// A hook violation is a test-logic error; make it fail the test that caused it rather than only logging to stderr.
	OperationTestHooks::CFaultHookScope::setViolationReporter([](const std::string& message) {
		FAIL_CHECK("Operation test hook violation: " << message);
	});

	{
		CRandomDataGenerator _randomGenerator;
		_randomGenerator.setSeed(g_randomSeed);
		Logger() << "RNG consustency check: seed = " << g_randomSeed <<", first RN = " << _randomGenerator.randomNumber<uint32_t>(0u, uint32_max);
	}

	return session.run();
}
