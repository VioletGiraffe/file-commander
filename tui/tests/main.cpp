// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <cstdio>
#include <thread>

// `--record <file> <arguments>...` stands in for the pager or the editor fcterm starts.
// Writes its working folder and its arguments to <file>, one per line. The file appears complete: it is renamed into place.
static int recordInvocation(const QStringList& arguments)
{
	const QString recordPath = arguments[2];
	QByteArray lines = "cwd=" + QDir::currentPath().toUtf8() + '\n';
	for (qsizetype i = 3; i < arguments.size(); ++i)
		lines += "arg=" + arguments[i].toUtf8() + '\n';

	QFile partialRecord{ recordPath + QStringLiteral(".part") };
	if (!partialRecord.open(QFile::WriteOnly) || partialRecord.write(lines) != lines.size())
		return 1;
	partialRecord.close();
	return partialRecord.rename(recordPath) ? 0 : 1;
}

// `--print-then-sleep <text>` stands in for a long-running program: prints <text>, sleeps for 30 s, then prints "finished".
// The process that printed <text> is the one an interrupt has to stop.
static int printThenSleep(const QString& text)
{
	std::fputs(qPrintable(text + u'\n'), stdout);
	std::fflush(stdout);
	std::this_thread::sleep_for(std::chrono::seconds(30));
	std::fputs("finished\n", stdout);
	return 0;
}

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() >= 3 && arguments[1] == QLatin1String("--record"))
		return recordInvocation(arguments);
	if (arguments.size() == 3 && arguments[1] == QLatin1String("--print-then-sleep"))
		return printThenSleep(arguments[2]);

	return runCatchSession(argc, argv);
}
