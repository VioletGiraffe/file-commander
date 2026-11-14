#include "cmainwindow.h"
#include "iconprovider/ciconprovider.h"
#include "themes/theme.h"

#include "ccontroller.h"
#include "pluginengine/cpluginengine.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "logger/cloggerinmemory.h"
#include "shelloperations/cshelloperationrunner.h"
#include "system/processfilepath.hpp"
#include "system/win_utils.hpp"
#include "theme/cthemecontroller.h"
#include "ui/widget-gallery/cwidgetgallery.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QImageReader>
#include <QSettings>
#include <QStringList>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace {
QtMessageHandler g_previousMessageHandler = nullptr;

// Global sink for every Qt message (qDebug/qInfo/qWarning/qCritical, plus our asserts, which route through qInfo).
// Captures into the in-memory log so "Report a bug" can display it, then reproduces the output the default handler
// would have written, so console/debugger visibility isn't lost. Fires on any thread; CLoggerInMemory is thread-safe.
void appMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	const QString formatted = qFormatLogMessage(type, context, message);
	loggerInstance<CLoggerInMemory>().log(formatted);

	if (g_previousMessageHandler)
		g_previousMessageHandler(type, context, message);
	else
	{
		fprintf(stderr, "%s\n", qUtf8Printable(formatted));
		fflush(stderr);
#ifdef _WIN32
		const QString line = formatted + QLatin1Char('\n');
		OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
#endif
	}

	if (type == QtFatalMsg)
		abort(); // Installing a handler suppresses Qt's own abort-on-fatal; keep the process terminating as before.
}

// Exit code of a --test-launch[=N] run, empty if not one: N is the plugin count the build produced.
std::optional<int> testLaunchExitCode(const QStringList& arguments, size_t loadedPluginCount)
{
	const QString countPrefix = "--test-launch=";
	for (const QString& argument : arguments)
	{
		if (argument == "--test-launch")
			return 0;
		if (!argument.startsWith(countPrefix))
			continue;

		bool countValid = false;
		const uint expectedPluginCount = argument.sliced(countPrefix.size()).toUInt(&countValid);
		if (!countValid)
		{
			qCritical().noquote() << "Invalid argument" << argument;
			return 1;
		}
		if (expectedPluginCount != loadedPluginCount)
		{
			qCritical().nospace() << "Test launch: " << loadedPluginCount << " plugins loaded, expected " << expectedPluginCount;
			return 1;
		}
		return 0;
	}

	return std::nullopt;
}

// Unix only: a Windows GUI program cannot run in the console it was started from
#ifndef _WIN32
constexpr std::string_view terminalUiArgument = "--tui";

// True for --tui, and where the GUI would not show on the user's screen. An explicit QT_QPA_PLATFORM needs no display.
// The Linux AppImage's AppRun makes the same choice first: this executable needs the system's X11 and GL libraries just to start.
[[nodiscard]] bool terminalUiRequested(int argc, char* argv[])
{
	if (std::any_of(argv + 1, argv + argc, [](const char* argument) { return argument == terminalUiArgument; }))
		return true;
	if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		return false;

#if defined __APPLE__
	return !qEnvironmentVariableIsEmpty("SSH_CONNECTION"); // Over SSH the GUI would open on the Mac's own screen
#else
	return qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#endif
}

// Replaces this process with fcterm, beside this executable, keeping its terminal and the arguments other than --tui.
// Returns only on failure, with this program's exit code.
[[nodiscard]] int runTerminalUi(int argc, char* argv[])
{
	const std::filesystem::path terminalUiPath = processFilePath().parent_path() / "fcterm";
	std::vector<char*> arguments{ const_cast<char*>(terminalUiPath.c_str()) };
	std::copy_if(argv + 1, argv + argc, std::back_inserter(arguments), [](const char* argument) { return argument != terminalUiArgument; });
	arguments.push_back(nullptr);
	execv(terminalUiPath.c_str(), arguments.data());
	std::fprintf(stderr, "Failed to start %s: %s\n", terminalUiPath.c_str(), std::strerror(errno));
	return 1;
}
#endif

// Removes the settings of the style, colour scheme and stylesheet options that the themes replaced.
// A pinned light or dark scheme carries over to CThemeController.
void migrateLegacyAppearanceSettings()
{
	const QString colorSchemeKey = QStringLiteral("Interface/ColorScheme");
	const QString styleGroup = QStringLiteral("Interface/Style"); // the style name, and the stylesheet text beneath it

	QSettings settings;
	const int colorScheme = settings.value(colorSchemeKey).toInt();
	if (colorScheme == static_cast<int>(Qt::ColorScheme::Light) || colorScheme == static_cast<int>(Qt::ColorScheme::Dark))
		CThemeController::instance().setSchemePreference(static_cast<Qt::ColorScheme>(colorScheme));

	settings.remove(colorSchemeKey);
	settings.remove(styleGroup);
}
}

int main(int argc, char *argv[])
{
#ifndef _WIN32
	if (terminalUiRequested(argc, argv))
		return runTerminalUi(argc, argv);
#endif

	g_previousMessageHandler = qInstallMessageHandler(appMessageHandler);

	AdvancedAssert::setLoggingFunc([](const char* message){
		qInfo() << message;
	});

	QImageReader::setAllocationLimit(2048); // raise Qt's 256 MB decode cap; 67 MP at 3x8 bits already exceeds it

	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	qInfo().nospace() << "Built with Qt " << QT_VERSION_STR << ", running with Qt " << qVersion();
	assert_r(std::string_view{QT_VERSION_STR} == qVersion());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
	QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

	QApplication app(argc, argv);

	// fcterm sets the same names: the two front ends share their settings
	QApplication::setApplicationName("File Commander");
	QApplication::setOrganizationName("GitHubSoft");

	migrateLegacyAppearanceSettings();
	installTheme(app);

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	{
		QFont font = QApplication::font();
		font.setPointSizeF(font.pointSizeF() + 1.0);
		QApplication::setFont(font);
	}

	CShellOperationRunner shellOperations;
	CController controller;
	CPluginEngine pluginEngine{controller};
	// Must outlive the window: its panel widgets unsubscribe from it when destroyed
	CIconProvider iconProvider;

	CMainWindow w{controller, pluginEngine, shellOperations, iconProvider};
	w.updateInterface();

	// For checking the themes against every stock control
	if (app.arguments().contains(QStringLiteral("--widget-gallery")))
		CWidgetGalleryWindow::showNew();

	if (const auto testLaunchResult = testLaunchExitCode(app.arguments(), pluginEngine.activePluginInfo().size()))
		QTimer::singleShot(5000, [code = *testLaunchResult] { qApp->exit(code); });

	const int exitCode = app.exec();
	// Must precede ~CMainWindow: the shell dialogs these operations put up are owned by its native window.
	shellOperations.waitForPendingOperations();
	controller.shutdown();
	return exitCode;
}
