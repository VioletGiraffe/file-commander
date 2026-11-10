#include "cmainwindow.h"
#include "cshelloperationrunner.h"
#include "iconprovider/ciconprovider.h"

#include "settings.h"
#include "ccontroller.h"
#include "pluginengine/cpluginengine.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "logger/cloggerinmemory.h"
#include "system/processfilepath.hpp"
#include "system/win_utils.hpp"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QImageReader>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QStyleHints>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
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
#elif defined _WIN32
	return false;
#else
	return qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#endif
}

// Hands over to fcterm, beside this executable, with the arguments other than --tui, and returns this program's exit code.
// Unix: fcterm replaces this process, keeping its terminal, so only a failure returns. Windows: this GUI program has no console, so fcterm gets its own.
[[nodiscard]] int runTerminalUi([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
#ifdef _WIN32
	const std::filesystem::path terminalUiPath = processFilePath().parent_path() / L"fcterm.exe";
	// argv is in the ANSI code page, which cannot hold every path
	int wideArgc = 0;
	wchar_t** wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
	QStringList arguments;
	for (int i = 1; wideArgv && i < wideArgc; ++i)
	{
		if (std::wstring_view{ wideArgv[i] } != L"--tui")
			arguments.push_back(QString::fromWCharArray(wideArgv[i]));
	}
	LocalFree(wideArgv);

	if (QProcess::startDetached(QString::fromStdWString(terminalUiPath.wstring()), arguments))
		return 0;
	std::fprintf(stderr, "Failed to start %s\n", terminalUiPath.string().c_str());
#else
	const std::filesystem::path terminalUiPath = processFilePath().parent_path() / "fcterm";
	std::vector<char*> arguments{ const_cast<char*>(terminalUiPath.c_str()) };
	std::copy_if(argv + 1, argv + argc, std::back_inserter(arguments), [](const char* argument) { return argument != terminalUiArgument; });
	arguments.push_back(nullptr);
	execv(terminalUiPath.c_str(), arguments.data());
	std::fprintf(stderr, "Failed to start %s: %s\n", terminalUiPath.c_str(), std::strerror(errno));
#endif
	return 1;
}
}

int main(int argc, char *argv[])
{
	if (terminalUiRequested(argc, argv))
		return runTerminalUi(argc, argv);

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

	QApplication::setApplicationName("File Commander");
	QApplication::setOrganizationName("GitHubSoft");

	// Init style and color scheme, if specified
	{
		QSettings s;
#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
		const int colorScheme = s.value(KEY_INTERFACE_COLOR_SCHEME, -1).toInt();
		if (colorScheme != -1)
			QApplication::styleHints()->setColorScheme(static_cast<Qt::ColorScheme>(colorScheme));
#endif

		const QString styleName = s.value(KEY_INTERFACE_STYLE).toString();
		if (!styleName.isEmpty())
			QApplication::setStyle(styleName);
	}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0) && defined(_WIN32)
	if (auto* styleHints = app.styleHints(); styleHints && styleHints->colorScheme() == Qt::ColorScheme::Dark)
	{
		// Fix the terrible default alternate row color on Windows
		QPalette p = QApplication::palette();
		QColor color = p.color(QPalette::Base);
		color = color.lighter(115);
		p.setColor(QPalette::AlternateBase, color);
		QApplication::setPalette(p);
	}
#endif

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	{
		QFont font = QApplication::font();
		font.setPointSizeF(font.pointSizeF() + 1.0);
		QApplication::setFont(font);
	}

	if (const auto styleSheet = QSettings{}.value(KEY_INTERFACE_STYLE_SHEET).toString(); !styleSheet.isEmpty())
		qApp->setStyleSheet(styleSheet);

	CShellOperationRunner shellOperations;
	CController controller;
	CPluginEngine pluginEngine{controller};
	// Must outlive the window: its panel widgets unsubscribe from it when destroyed
	CIconProvider iconProvider;

	CMainWindow w{controller, pluginEngine, shellOperations, iconProvider};
	w.updateInterface();

	if (const auto testLaunchResult = testLaunchExitCode(app.arguments(), pluginEngine.activePluginInfo().size()))
		QTimer::singleShot(5000, [code = *testLaunchResult] { qApp->exit(code); });

	const int exitCode = app.exec();
	// Must precede ~CMainWindow: the shell dialogs these operations put up are owned by its native window.
	shellOperations.waitForPendingOperations();
	controller.shutdown();
	return exitCode;
}
