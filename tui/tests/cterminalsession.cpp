#include "cterminalsession.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QDir>
#include <QFile>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined __APPLE__
#include <util.h>
#elif defined __FreeBSD__
#include <libutil.h>
#else
#include <pty.h>
#endif
#endif

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

#ifdef _WIN32
// One argument as CommandLineToArgvW reads it back
static QString quotedArgument(const QString& argument)
{
	if (!argument.isEmpty() && !argument.contains(u' ') && !argument.contains(u'\t') && !argument.contains(u'"'))
		return argument;

	QString quoted = QStringLiteral("\"");
	qsizetype backslashes = 0;
	for (const QChar c : argument)
	{
		if (c == u'\\')
		{
			++backslashes;
			continue;
		}
		// Backslashes double only before a quote
		quoted += QString(c == u'"' ? backslashes * 2 + 1 : backslashes, u'\\');
		backslashes = 0;
		quoted += c;
	}
	quoted += QString(backslashes * 2, u'\\');
	quoted += u'"';
	return quoted;
}

CTerminalSession::CTerminalSession(const Program& program, int rows, int columns) :
	_screen(rows, columns)
{
	HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
	// The large buffer keeps ConPTY from blocking between two reads
	if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) || !CreatePipe(&outputRead, &outputWrite, nullptr, 1 << 20))
	{
		_startError = QStringLiteral("CreatePipe failed: %1").arg(GetLastError());
		return;
	}

	HPCON pseudoConsole = nullptr;
	const HRESULT created = CreatePseudoConsole(COORD{ (SHORT)columns, (SHORT)rows }, inputRead, outputWrite, 0, &pseudoConsole);
	// The pseudo console holds its own copies
	CloseHandle(inputRead);
	CloseHandle(outputWrite);
	_input = inputWrite;
	_output = outputRead;
	if (FAILED(created))
	{
		_startError = QStringLiteral("CreatePseudoConsole failed: 0x%1").arg((unsigned long)created, 0, 16);
		return;
	}
	_pseudoConsole = pseudoConsole;

	SIZE_T attributeListSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
	std::vector<std::byte> attributeListStorage(attributeListSize);
	auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListStorage.data());
	InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize);
	UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole, sizeof(pseudoConsole), nullptr, nullptr);

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.lpAttributeList = attributeList;
	// Null standard handles: the program gets the pseudo console's. Else it gets this process's, which a test runner redirects.
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

	QString commandLine = quotedArgument(QDir::toNativeSeparators(program.path));
	for (const QString& argument : program.arguments)
		commandLine += u' ' + quotedArgument(argument);
	std::wstring writableCommandLine = commandLine.toStdWString();

	QString environmentBlock;
	for (const QString& variable : program.environment.toStringList())
		environmentBlock += variable + QChar{ 0 };
	environmentBlock += QChar{ 0 };

	const QString workingDir = QDir::toNativeSeparators(program.workingDir);
	PROCESS_INFORMATION processInfo{};
	const BOOL started = CreateProcessW(nullptr, writableCommandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
		(void*)environmentBlock.utf16(), (const wchar_t*)workingDir.utf16(), &startup.StartupInfo, &processInfo);
	DeleteProcThreadAttributeList(attributeList);
	if (!started)
	{
		_startError = QStringLiteral("CreateProcess failed: %1").arg(GetLastError());
		return;
	}

	CloseHandle(processInfo.hThread);
	_process = processInfo.hProcess;
}

CTerminalSession::~CTerminalSession()
{
	if (_process)
	{
		if (!hasExited())
		{
			TerminateProcess(_process, 1);
			WaitForSingleObject(_process, 5000);
		}
		CloseHandle(_process);
	}

	if (_input)
		CloseHandle(_input);
	if (_pseudoConsole)
	{
		// Before Windows 11 24H2, ClosePseudoConsole blocks until its last output is read
		std::thread drainOutput{ [output = _output] {
			char buffer[4096];
			DWORD bytesRead = 0;
			while (ReadFile(output, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {}
		} };
		ClosePseudoConsole((HPCON)_pseudoConsole);
		drainOutput.join();
	}
	if (_output)
		CloseHandle(_output);
}

bool CTerminalSession::readOutput(std::chrono::milliseconds wait)
{
	const auto deadline = Clock::now() + wait;
	DWORD available = 0;
	for (;;)
	{
		if (!PeekNamedPipe(_output, nullptr, 0, nullptr, &available, nullptr))
			return false;
		if (available > 0)
			break;
		if (Clock::now() >= deadline)
			return true;
		std::this_thread::sleep_for(5ms);
	}

	char buffer[65536];
	while (available > 0)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(_output, buffer, std::min<DWORD>(available, sizeof(buffer)), &bytesRead, nullptr) || bytesRead == 0)
			return false;
		_screen.feed(QString(_decoder.decode(QByteArrayView{ buffer, (qsizetype)bytesRead })));
		available -= bytesRead;
	}

	if (const QByteArray replies = _screen.takeReplies(); !replies.isEmpty())
		write(replies.constData(), (size_t)replies.size());
	return true;
}

void CTerminalSession::write(const char* data, size_t size)
{
	DWORD written = 0;
	WriteFile(_input, data, (DWORD)size, &written, nullptr);
}

bool CTerminalSession::hasExited()
{
	if (_exitCode)
		return true;
	if (!_process || WaitForSingleObject(_process, 0) != WAIT_OBJECT_0)
		return false;

	DWORD exitCode = 0;
	GetExitCodeProcess(_process, &exitCode);
	_exitCode = (int)exitCode;
	return true;
}

#else

CTerminalSession::CTerminalSession(const Program& program, int rows, int columns) :
	_screen(rows, columns)
{
	// Everything the child needs exists before the fork: between fork and exec only async-signal-safe calls are allowed
	const QByteArray path = QFile::encodeName(program.path);
	const QByteArray workingDir = QFile::encodeName(program.workingDir);
	std::vector<QByteArray> argumentBytes{ path };
	for (const QString& argument : program.arguments)
		argumentBytes.push_back(argument.toLocal8Bit());
	std::vector<QByteArray> environmentBytes;
	for (const QString& variable : program.environment.toStringList())
		environmentBytes.push_back(variable.toLocal8Bit());

	std::vector<char*> arguments, environment;
	for (QByteArray& argument : argumentBytes)
		arguments.push_back(argument.data());
	arguments.push_back(nullptr);
	for (QByteArray& variable : environmentBytes)
		environment.push_back(variable.data());
	environment.push_back(nullptr);

	// Carries the child's errno when exec fails; closes on a successful exec
	int execErrorPipe[2];
	if (::pipe(execErrorPipe) != 0)
	{
		_startError = QStringLiteral("pipe failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
		return;
	}
	::fcntl(execErrorPipe[1], F_SETFD, FD_CLOEXEC);

	winsize size{};
	size.ws_row = (unsigned short)rows;
	size.ws_col = (unsigned short)columns;
	_pid = ::forkpty(&_terminal, nullptr, nullptr, &size);
	if (_pid == 0)
	{
		::close(execErrorPipe[0]);
		if (::chdir(workingDir.constData()) == 0)
			::execve(path.constData(), arguments.data(), environment.data());
		const int error = errno;
		[[maybe_unused]] const auto written = ::write(execErrorPipe[1], &error, sizeof(error));
		::_exit(127);
	}

	::close(execErrorPipe[1]);
	if (_pid < 0)
	{
		_startError = QStringLiteral("forkpty failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
		::close(execErrorPipe[0]);
		return;
	}

	int execError = 0;
	ssize_t bytesRead = 0;
	do
		bytesRead = ::read(execErrorPipe[0], &execError, sizeof(execError));
	while (bytesRead < 0 && errno == EINTR);
	::close(execErrorPipe[0]);
	if (bytesRead == (ssize_t)sizeof(execError))
	{
		_startError = QStringLiteral("Cannot start %1: %2").arg(program.path, QString::fromLocal8Bit(std::strerror(execError)));
		return;
	}

	::fcntl(_terminal, F_SETFL, ::fcntl(_terminal, F_GETFL) | O_NONBLOCK);
}

CTerminalSession::~CTerminalSession()
{
	const bool running = _pid > 0 && !hasExited();
	if (running)
		::kill(_pid, SIGKILL);
	// Closed before the wait: an exiting program may wait for its unread terminal output to drain.
	// The hangup also ends whatever the program left running on the terminal.
	if (_terminal >= 0)
		::close(_terminal);
	if (running)
		::waitpid(_pid, nullptr, 0);
}

bool CTerminalSession::readOutput(std::chrono::milliseconds wait)
{
	pollfd terminal{ _terminal, POLLIN, 0 };
	if (::poll(&terminal, 1, (int)wait.count()) <= 0)
		return true;

	char buffer[65536];
	for (;;)
	{
		const ssize_t bytesRead = ::read(_terminal, buffer, sizeof(buffer));
		if (bytesRead > 0)
		{
			_screen.feed(QString(_decoder.decode(QByteArrayView{ buffer, (qsizetype)bytesRead })));
			continue;
		}
		if (bytesRead < 0 && errno == EINTR)
			continue;
		if (bytesRead < 0 && errno == EAGAIN)
			break;
		return false; // EIO: nothing has the terminal open on the program's side
	}

	if (const QByteArray replies = _screen.takeReplies(); !replies.isEmpty())
		write(replies.constData(), (size_t)replies.size());
	return true;
}

void CTerminalSession::write(const char* data, size_t size)
{
	while (size > 0)
	{
		const ssize_t written = ::write(_terminal, data, size);
		if (written > 0)
		{
			data += written;
			size -= (size_t)written;
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		if (written < 0 && errno != EAGAIN)
			return;

		pollfd terminal{ _terminal, POLLOUT, 0 };
		::poll(&terminal, 1, 100);
	}
}

bool CTerminalSession::hasExited()
{
	if (_exitCode)
		return true;

	int status = 0;
	if (_pid <= 0 || ::waitpid(_pid, &status, WNOHANG) != _pid)
		return false;

	_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
	return true;
}

#endif

void CTerminalSession::send(std::string_view keys)
{
	write(keys.data(), keys.size());
}

void CTerminalSession::send(const QString& text)
{
	const QByteArray utf8 = text.toUtf8();
	write(utf8.constData(), (size_t)utf8.size());
}

bool CTerminalSession::waitFor(const std::function<bool(const CTerminalScreen&)>& condition, std::chrono::milliseconds timeout)
{
	const auto deadline = Clock::now() + timeout;
	for (;;)
	{
		if (condition(_screen))
			return true;

		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
		if (remaining <= 0ms)
			return false;
		if (!readOutput(std::min(remaining, 50ms)))
			return condition(_screen);
	}
}

std::optional<int> CTerminalSession::waitForExit(std::chrono::milliseconds timeout)
{
	const auto deadline = Clock::now() + timeout;
	while (!hasExited())
	{
		if (Clock::now() >= deadline)
			return std::nullopt;
		// The program can block on a full terminal until its output is read
		if (!readOutput(50ms))
			std::this_thread::sleep_for(10ms);
	}
	return _exitCode;
}
