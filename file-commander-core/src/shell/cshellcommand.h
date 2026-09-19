#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringDecoder>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <functional>

// One command line run through the platform shell, stdout and stderr merged; see doc/process-launching.md.
// The shell and everything it launches share one job object (Windows) or process group (POSIX).
// Every callback runs on the thread that owns this object.
class CShellCommand
{
public:
	CShellCommand(QString command, QString workingDir);
	// Terminates a command still running: only a quit that skips the main window's close event gets here with one.
	~CShellCommand();

	CShellCommand(const CShellCommand&) = delete;
	CShellCommand& operator=(const CShellCommand&) = delete;

	// The longest `command` the shell runs in `workingDir`
	[[nodiscard]] static qsizetype maxCommandLength(const QString& workingDir);

	// Decoded output in the pieces it arrives in, which need not end at a line break
	std::function<void(const QString& text)> onOutput;
	// Once, after the shell exits. Never called when start() fails.
	std::function<void(int exitCode, bool normalExit)> onFinished;

	// The error is the reason, readable by the user
	[[nodiscard]] std::expected<void, QString> start();
	// Ends the shell and everything it launched. No-op once the shell has exited.
	void terminateTree();
	// terminateTree with SIGKILL on POSIX, where terminateTree sends SIGTERM; identical on Windows
	void killTree();

	[[nodiscard]] bool isRunning() const;
	[[nodiscard]] const QString& command() const;

private:
	void forwardOutput();
	[[nodiscard]] QString decodedOutput(const QByteArray& bytes);
#ifdef _WIN32
	[[nodiscard]] QString decodedFromOemCodePage(const QByteArray& bytes);
#endif

	const QString _command;
	const QString _workingDir;
	QProcess _process;

	QStringDecoder _utf8Decoder{ QStringDecoder::Utf8 };
	bool _legacyEncoding = false; // Set by the first invalid UTF-8 sequence, for the rest of the output
#ifdef _WIN32
	QByteArray _pendingOemLeadByte; // A double-byte character's lead byte that ended the previous chunk
	void* _job = nullptr; // HANDLE, null until start()
#else
	QStringDecoder _localeDecoder{ QStringDecoder::System };
#endif
};
