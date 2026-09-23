#define NO_TEST_MAIN
#include "3rdparty/catch2/test_main.hpp" // First: compiles catch.hpp with the runner


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	// An ini file of our own: the storage tests must neither read nor overwrite the user's real Programs menu
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath() % QStringLiteral("/file-commander-userprograms-test-settings"));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QCoreApplication::setOrganizationName(QStringLiteral("file-commander-tests"));
	QCoreApplication::setApplicationName(QStringLiteral("userprograms_test"));
	QSettings{}.clear();

	return runCatchSession(argc, argv);
}
