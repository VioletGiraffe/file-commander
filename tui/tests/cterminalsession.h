#pragma once

#include "cterminalscreen.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

// Keys as a VT terminal sends them
namespace TerminalKeys {
inline constexpr std::string_view Enter = "\r", Escape = "\x1b", Tab = "\t", Up = "\x1b[A", Down = "\x1b[B", Home = "\x1b[H", End = "\x1b[F", Insert = "\x1b[2~", Delete = "\x1b[3~", Backspace = "\x7f",
	F2 = "\x1bOQ", ShiftF2 = "\x1b[1;2Q", F3 = "\x1bOR", ShiftF3 = "\x1b[1;2R", F4 = "\x1bOS", F5 = "\x1b[15~", F7 = "\x1b[18~", F10 = "\x1b[21~", ShiftF7 = "\x1b[18;2~", ShiftF8 = "\x1b[19;2~", AltF7 = "\x1b[18;3~",
	AltF12 = "\x1b[24;3~", CtrlF5 = "\x1b[15;5~", CtrlF6 = "\x1b[17;5~",
	Alt2 = "\x1b" "2", AltC = "\x1b" "c", AltD = "\x1b" "d", AltE = "\x1b" "e", AltF = "\x1b" "f", AltN = "\x1b" "n", AltO = "\x1b" "o", AltP = "\x1b" "p", AltR = "\x1b" "r", AltS = "\x1b" "s", AltU = "\x1b" "u", AltV = "\x1b" "v", AltX = "\x1b" "x",
	CtrlB = "\x02", CtrlC = "\x03", CtrlD = "\x04", CtrlHome = "\x1b[1;5H", CtrlF = "\x06", CtrlG = "\x07", CtrlL = "\x0c", CtrlO = "\x0f", CtrlQ = "\x11", CtrlS = "\x13", CtrlT = "\x14", CtrlW = "\x17", CtrlPgUp = "\x1b[5;5~", CtrlPgDn = "\x1b[6;5~", CtrlUp = "\x1b[1;5A";
}

// Runs a program in a pseudo-terminal, as a terminal window would, and keeps the screen it draws.
// Unix: forkpty. Windows: ConPTY, which translates the keys into console input and the console into VT output.
class CTerminalSession
{
public:
	struct Program
	{
		QString path;
		QStringList arguments;
		QString workingDir;
		QProcessEnvironment environment;
	};

	static constexpr std::chrono::milliseconds defaultTimeout{ 10'000 };

	CTerminalSession(const Program& program, int rows, int columns);
	// Kills the program if it still runs
	~CTerminalSession();
	CTerminalSession(const CTerminalSession&) = delete;
	CTerminalSession& operator=(const CTerminalSession&) = delete;

	// Empty when the program started
	[[nodiscard]] const QString& startError() const noexcept { return _startError; }

	void send(std::string_view keys);
	void send(const QString& text);

	// Feeds the program's output to the screen until the condition holds; false when the timeout passes first
	[[nodiscard]] bool waitFor(const std::function<bool(const CTerminalScreen&)>& condition, std::chrono::milliseconds timeout = defaultTimeout);
	// The exit code; nullopt when the program still runs after the timeout
	[[nodiscard]] std::optional<int> waitForExit(std::chrono::milliseconds timeout = defaultTimeout);

	[[nodiscard]] const CTerminalScreen& screen() const noexcept { return _screen; }

private:
	// Feeds the screen the output that arrives within `wait`; false once the program's end of the terminal is closed
	bool readOutput(std::chrono::milliseconds wait);
	void write(const char* data, size_t size);
	[[nodiscard]] bool hasExited();
#ifdef _WIN32
	// Runs on _outputReader
	void collectOutputUntilClosed();
#endif

private:
	CTerminalScreen _screen;
	QStringDecoder _decoder{ QStringDecoder::Utf8 };
	QString _startError;
	std::optional<int> _exitCode;

#ifdef _WIN32
	void* _pseudoConsole = nullptr;
	void* _input = nullptr;
	void* _output = nullptr;
	void* _process = nullptr;

	// Reads _output from the pseudo console's creation until it closes: ConPTY blocks while its output is unread
	std::thread _outputReader;
	std::mutex _outputMutex;
	std::condition_variable _outputArrived;
	QByteArray _unreadOutput; // Guarded by _outputMutex
	bool _outputClosed = false; // Guarded by _outputMutex
#else
	int _terminal = -1;
	int _pid = -1;
#endif
};
