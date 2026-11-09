#include "ctuiapplication.h"

#include "ccontroller.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

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

	QFile logFile{ QDir::tempPath() % QStringLiteral("/file-commander-tui.log") };
	if (logFile.open(QFile::WriteOnly | QFile::Truncate))
		g_logFile = &logFile;
	qInstallMessageHandler(logToFile);
	AdvancedAssert::setLoggingFunc([](const char* message) { qInfo() << message; });

	{
		CController controller;
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
