#define CATCH_CONFIG_RUNNER

#include "settings/csettings.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main(int argc, char* argv[])
{
	// CPanel's filesystem watcher installs a native event filter, which needs an application instance. No GUI is
	// initialized, so this suite needs no platform plugin and no display.
	QCoreApplication app{ argc, argv };

#if defined _WIN32 && defined _DEBUG
	// Some cases drive paths that assert recoverably; the CRT assert must report to stderr instead of opening an
	// interactive dialog.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

	// An ini file of our own: the panel reads KEY_INTERFACE_SHOW_HIDDEN_FILES on every listing, and that must
	// neither come from nor land in the user's real File Commander settings.
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath() % QStringLiteral("/file-commander-panel-test-settings"));
	CSettings::setFormat(QSettings::IniFormat);
	CSettings::setOrganizationName(QStringLiteral("file-commander-tests"));
	CSettings::setApplicationName(QStringLiteral("panel_test"));
	CSettings{}.clear();

	return Catch::Session().run(argc, argv);
}
