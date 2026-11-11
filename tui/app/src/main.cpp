#include "ctuiapplication.h"

#include "ccontroller.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace {
std::mutex g_logMutex;
QFile* g_logFile = nullptr; // Guarded by g_logMutex

// Fires on any thread. stderr would draw over the terminal UI, so messages go to a file.
void logToFile(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	const QByteArray line = qFormatLogMessage(type, context, message).toUtf8() + '\n';
	{
		std::lock_guard lock{ g_logMutex };
		if (g_logFile)
		{
			g_logFile->write(line);
			g_logFile->flush();
		}
	}

	if (type == QtFatalMsg)
		abort(); // Installing a handler suppresses Qt's own abort on a fatal message
}
}

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };
	// Not the GUI's names: the controller saves its tabs in these settings, and must not overwrite the GUI's
	QCoreApplication::setOrganizationName(QStringLiteral("GitHubSoft"));
	QCoreApplication::setApplicationName(QStringLiteral("File Commander TUI"));

	QCommandLineParser parser;
	parser.addHelpOption();
	const QCommandLineOption settingsDirOption{ QStringLiteral("settings-dir"), QStringLiteral("Keeps the settings in an INI file under <folder>."), QStringLiteral("folder") };
	parser.addOption(settingsDirOption);
	parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("The folder to open on the left side; the working folder by default."), QStringLiteral("[folder]"));
	parser.process(app);
	if (parser.isSet(settingsDirOption))
	{
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, parser.value(settingsDirOption));
	}

	const QStringList folderArguments = parser.positionalArguments();
	if (folderArguments.size() > 1)
		parser.showHelp(1);
	const QString startFolder = folderArguments.isEmpty() ? QDir::currentPath() : QDir::cleanPath(QFileInfo{ folderArguments.front() }.absoluteFilePath());
	if (!QFileInfo{ startFolder }.isDir())
	{
		std::fprintf(stderr, "Not a folder: %s\n", qUtf8Printable(QDir::toNativeSeparators(startFolder)));
		return 1;
	}

	QFile logFile{ QDir::tempPath() % QStringLiteral("/file-commander-tui.log") };
	if (logFile.open(QFile::WriteOnly | QFile::Truncate))
		g_logFile = &logFile;
	qInstallMessageHandler(logToFile);
	AdvancedAssert::setLoggingFunc([](const char* message) { qInfo() << message; });

#ifdef _WIN32
	// Turbo Vision detects only 16 colours on a Windows console unless COLORTERM is set: magiblot/tvision#249
	if (qEnvironmentVariableIsEmpty("COLORTERM"))
		qputenv("COLORTERM", "truecolor");
#endif

	{
		CController controller;
		controller.activateOrAddTabShowing(Panel::LeftPanel, startFolder); // The terminal UI starts on the left side
		CTuiApplication tui{ controller };
		tui.run();

		controller.shutdown();
		tui.shutDown();
	}

	qInstallMessageHandler(nullptr);
	{
		std::lock_guard lock{ g_logMutex };
		g_logFile = nullptr;
	}

	return 0;
}
