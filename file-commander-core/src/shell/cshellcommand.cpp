#include "cshellcommand.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <utility>

#ifdef _WIN32
#include "system/win_utils.hpp"
#include "utility/on_scope_exit.hpp"

#include <Windows.h>

#include <cstddef>
#include <vector>
#else
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

CShellCommand::CShellCommand(QString command, QString workingDir) :
	_command{ std::move(command) },
	_workingDir{ std::move(workingDir) }
{
	_process.setProcessChannelMode(QProcess::MergedChannels);

	QObject::connect(&_process, &QProcess::readyReadStandardOutput, &_process, [this] { forwardOutput(); });
	QObject::connect(&_process, &QProcess::finished, &_process, [this](int exitCode, QProcess::ExitStatus exitStatus) {
		forwardOutput();
		if (onFinished)
			onFinished(exitCode, exitStatus == QProcess::NormalExit);
	});
}

CShellCommand::~CShellCommand()
{
	// Owners destroy their commands while tearing themselves down, so nothing may call back into them from here
	onOutput = nullptr;
	onFinished = nullptr;

	if (isRunning())
	{
		terminateTree();
		_process.waitForFinished(1000);
	}

#ifdef _WIN32
	if (_job)
		::CloseHandle(static_cast<HANDLE>(_job));
#endif
}

std::expected<void, QString> CShellCommand::start()
{
	assert_debug_only(!isRunning());

	const auto failure = [this](const QString& reason) {
		qInfo().noquote() << "Failed to start the command" << _command << "in" << _workingDir << ':' << reason;
		return std::unexpected{ reason };
	};

#ifdef _WIN32
	// No JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: a program the command started with `start` must outlive this object
	_job = ::CreateJobObjectW(nullptr, nullptr);
	if (!_job)
		return failure(QStringLiteral("CreateJobObjectW failed: ") % QString::fromStdString(ErrorStringFromLastError()));

	SIZE_T attributeListSize = 0;
	::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
	std::vector<std::byte> attributeListStorage(attributeListSize);
	const auto attributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(static_cast<void*>(attributeListStorage.data()));
	if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize))
		return failure(QStringLiteral("InitializeProcThreadAttributeList failed: ") % QString::fromStdString(ErrorStringFromLastError()));
	EXEC_ON_SCOPE_EXIT([attributeList] { ::DeleteProcThreadAttributeList(attributeList); });

	// Joined at creation: a job assigned after start misses whatever the shell launches first
	if (!::UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &_job, sizeof(HANDLE), nullptr, nullptr))
		return failure(QStringLiteral("UpdateProcThreadAttribute failed: ") % QString::fromStdString(ErrorStringFromLastError()));

	STARTUPINFOEXW startupInfo{};
	_process.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments* arguments) {
		startupInfo.StartupInfo = *arguments->startupInfo;
		startupInfo.StartupInfo.cb = sizeof(startupInfo);
		startupInfo.lpAttributeList = attributeList;
		arguments->startupInfo = &startupInfo.StartupInfo;
		arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
	});
	EXEC_ON_SCOPE_EXIT([this] { _process.setCreateProcessArgumentsModifier({}); }); // The modifier captures locals

	// cmd does not support a UNC current directory: pushd applies the working dir instead of setWorkingDirectory, and maps a
	// temporary drive letter for UNC.
	// /s: cmd strips only the outermost quotes and takes the rest verbatim, so the working dir can be quoted.
	_process.setProgram(QStringLiteral("cmd.exe"));
	_process.setNativeArguments(QStringLiteral("/s /c \"pushd \"") % _workingDir % QStringLiteral("\" && ") % _command % '\"');
#else
	_process.setProgram(QStringLiteral("/bin/sh"));
	_process.setArguments({ QStringLiteral("-c"), _command });
	_process.setWorkingDirectory(_workingDir);
	// Runs in the child before exec, so the group exists before the shell can launch anything
	_process.setChildProcessModifier([] { ::setpgid(0, 0); });
#endif

	// Qt passes CREATE_NO_WINDOW when the parent has no console, so no console window appears on Windows
	_process.start();
	if (!_process.waitForStarted())
		return failure(_process.errorString());

	return {};
}

void CShellCommand::terminateTree()
{
	// Once the shell has exited, the job may still hold programs started with `start`, and kill() reads pid 0 as this app's own group
	if (!isRunning())
		return;

#ifdef _WIN32
	::TerminateJobObject(static_cast<HANDLE>(_job), 1);
#else
	// SIGTERM lets the processes clean up; one that ignores it keeps running
	::kill(-static_cast<pid_t>(_process.processId()), SIGTERM);
#endif
}

void CShellCommand::killTree()
{
#ifdef _WIN32
	terminateTree();
#else
	// processId() is 0 once the shell has exited, and kill() reads pid 0 as this app's own process group
	if (!isRunning())
		return;

	::kill(-static_cast<pid_t>(_process.processId()), SIGKILL);
#endif
}

bool CShellCommand::isRunning() const
{
	return _process.state() != QProcess::NotRunning;
}

const QString& CShellCommand::command() const
{
	return _command;
}

void CShellCommand::forwardOutput()
{
	const QString text = decodedOutput(_process.readAllStandardOutput());
	if (onOutput && !text.isEmpty())
		onOutput(text);
}

// UTF-8 until the first invalid sequence, then the shell's legacy encoding for the rest of the output
QString CShellCommand::decodedOutput(const QByteArray& bytes)
{
	if (!_legacyEncoding)
	{
		QString text = _utf8Decoder.decode(bytes);
		if (!_utf8Decoder.hasError())
			return text;

		_legacyEncoding = true; // The chunk is decoded again below
	}

#ifdef _WIN32
	return decodedFromOemCodePage(bytes);
#else
	return _localeDecoder.decode(bytes);
#endif
}

#ifdef _WIN32
// cmd and console programs write to a pipe in the console code page, which a new console starts with set to the OEM one
QString CShellCommand::decodedFromOemCodePage(const QByteArray& bytes)
{
	QByteArray input = std::exchange(_pendingOemLeadByte, {}) + bytes;

	qsizetype i = 0;
	while (i < input.size())
		i += ::IsDBCSLeadByteEx(CP_OEMCP, static_cast<BYTE>(input[i])) ? 2 : 1;

	if (i > input.size()) // The last byte is a lead byte whose trail byte is in the next chunk
	{
		_pendingOemLeadByte = input.last(1);
		input.chop(1);
	}

	if (input.isEmpty())
		return {};

	const int length = ::MultiByteToWideChar(CP_OEMCP, 0, input.constData(), static_cast<int>(input.size()), nullptr, 0);
	QString text(length, Qt::Uninitialized);
	::MultiByteToWideChar(CP_OEMCP, 0, input.constData(), static_cast<int>(input.size()), reinterpret_cast<wchar_t*>(text.data()), length);
	return text;
}
#endif
