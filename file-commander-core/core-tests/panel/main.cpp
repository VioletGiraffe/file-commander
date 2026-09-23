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
	// CPanel's filesystem watcher installs a native event filter, which needs an application instance. No GUI is
	// initialized, so this suite needs no platform plugin and no display.
	QCoreApplication app{ argc, argv };

	// An ini file of our own: the panel reads KEY_INTERFACE_SHOW_HIDDEN_FILES on every listing, and that must
	// neither come from nor land in the user's real File Commander settings.
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath() % QStringLiteral("/file-commander-panel-test-settings"));
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QCoreApplication::setOrganizationName(QStringLiteral("file-commander-tests"));
	QCoreApplication::setApplicationName(QStringLiteral("panel_test"));
	QSettings{}.clear();

	return runCatchSession(argc, argv);
}
