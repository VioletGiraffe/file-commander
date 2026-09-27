#include "fileoperations/operationtesthooks.h"


// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	// A headless caller selects the offscreen platform through QT_QPA_PLATFORM (the CI Linux run line does).
	QApplication app{ argc, argv };

	// A hook violation is a test-logic error; make it fail the test that caused it rather than only logging to stderr.
	OperationTestHooks::CFaultHookScope::setViolationReporter([](const std::string& message) {
		FAIL_CHECK("Operation test hook violation: " << message);
	});

	return runCatchSession(argc, argv);
}
