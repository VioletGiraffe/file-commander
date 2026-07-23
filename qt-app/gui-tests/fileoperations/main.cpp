#define CATCH_CONFIG_RUNNER
#include "3rdparty/catch2/catch.hpp"

#include "fileoperations/operationtesthooks.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main(int argc, char* argv[])
{
	// A headless caller selects the offscreen platform through QT_QPA_PLATFORM (the CI Linux run line does).
	QApplication app{ argc, argv };

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

	return Catch::Session().run(argc, argv);
}
