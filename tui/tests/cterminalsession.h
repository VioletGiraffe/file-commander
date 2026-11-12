#pragma once

#include "cterminalscreen.h"

DISABLE_COMPILER_WARNINGS
#include <QProcessEnvironment>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>

// Keys as a VT terminal sends them
namespace TerminalKeys {
inline constexpr std::string_view Enter = "\r", Escape = "\x1b", Tab = "\t", Down = "\x1b[B", Home = "\x1b[H", Insert = "\x1b[2~",
	F2 = "\x1bOQ", F3 = "\x1bOR", F4 = "\x1bOS", F5 = "\x1b[15~", F7 = "\x1b[18~", F10 = "\x1b[21~", ShiftF7 = "\x1b[18;2~", ShiftF8 = "\x1b[19;2~", AltF7 = "\x1b[18;3~",
	CtrlF5 = "\x1b[15;5~", CtrlF6 = "\x1b[17;5~",
	Alt2 = "\x1b" "2", AltC = "\x1b" "c", AltD = "\x1b" "d", AltF = "\x1b" "f", AltS = "\x1b" "s", AltX = "\x1b" "x",
	CtrlC = "\x03", CtrlF = "\x06", CtrlO = "\x0f", CtrlS = "\x13", CtrlT = "\x14", CtrlW = "\x17", CtrlPgUp = "\x1b[5;5~";
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
#else
	int _terminal = -1;
	int _pid = -1;
#endif
};
