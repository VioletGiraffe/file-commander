#include "cshell.h"

#include "filesystemhelperfunctions.h"
#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "system/win_utils.hpp"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <string.h> // memset

#ifdef _WIN32
#include "windows_path_win.hpp" // thin_io

#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wrl/client.h>
#endif

namespace {

std::pair<QString /* exe path */, QString /* args */> parseCommandAndArguments(const QString& cmdLine)
{
	QStringList argsList = QProcess::splitCommand(cmdLine);
	assert_and_return_r(!argsList.empty(), {});
	QString cmd = std::move(argsList.front());
	argsList.pop_front();

	QString argsString;
	for (auto&& str : argsList)
	{
		if (!argsString.isEmpty())
			argsString += ' ';

		if (str.contains(' '))
			argsString += ('\"' % str % '\"');
		else
			argsString += str;
	}

	return { std::move(cmd), std::move(argsString) };
}

QString defaultShellExecutableCommand()
{
#ifdef _WIN32
	static constexpr const char* knownTerminals[][2]{
		{ "wt.exe", "-d %dir%" }, // Windows Terminal
		{ "pwsh.exe", nullptr }, // New powershell?
		{ "powershell.exe", nullptr }, // Classic powershell
		{ "cmd.exe", nullptr }
	};
#elif defined __linux__
	static constexpr const char* knownTerminals[][2]{
		{ "/usr/bin/konsole", nullptr }, // KDE
		{ "/usr/bin/gnome-terminal", nullptr }, // Gnome
		{ "/usr/bin/pantheon-terminal", nullptr }, // Pantheon (Elementary OS)
		{ "/usr/bin/qterminal", nullptr }, // QTerminal under linux
		{ "/usr/local/bin/qterminal", nullptr }, // QTerminal under freebsd
		{ "/usr/bin/lxterminal", "--working-directory=%dir%" }
	};
#elif defined __APPLE__
	static constexpr const char* knownTerminals[][2]{
		{ "/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal", nullptr },
	};
#else
#pragma message("unknown platform")
	static constexpr const char* knownTerminals[][2]{
		{ "", nullptr }
	};
#endif

	for (const auto& candidate : knownTerminals)
	{
		const QString qstring = candidate[0];
		if (QFile::exists(qstring) || OsShell::isInPath(qstring))
			return candidate[1] ? (qstring + ' ' + candidate[1]) : qstring;
	}

	return {};
}

} // namespace

std::pair<QString /* exe path */, QString /* args */> OsShell::shellExecutable()
{
	//const QString shell = QSettings{}.value(KEY_OTHER_SHELL_COMMAND_NAME, defaultShellExecutableCommand()).toString();
	auto shell = defaultShellExecutableCommand();
	return parseCommandAndArguments(shell);
}

#ifdef _WIN32
// Resolves `program` in cmd's search order: the working dir unless NoDefaultCurrentDirectoryInExePath excludes it, then PATH,
// each directory trying the PATHEXT extensions when the name has none. Empty when nothing matches.
static QString resolvedProgramPath(const QString& program, const QString& workingDir)
{
	QStringList directories;
	if (::NeedCurrentDirectoryForExePathW(reinterpret_cast<const wchar_t*>(program.utf16())))
		directories += workingDir;
	if (!program.contains('\\') && !program.contains('/'))
		directories += qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts);

	const QStringList extensions = QFileInfo{ program }.suffix().isEmpty()
		? qEnvironmentVariable("PATHEXT", QStringLiteral(".COM;.EXE;.BAT;.CMD")).split(';', Qt::SkipEmptyParts)
		: QStringList{ QString{} };

	for (const QString& directory : directories)
	{
		for (const QString& extension : extensions)
		{
			if (const QFileInfo candidate{ QDir{ directory }.absoluteFilePath(program + extension) }; candidate.isFile())
				return candidate.absoluteFilePath();
		}
	}

	return {};
}

std::expected<std::optional<OsShell::ProgramInvocation>, OsShell::GuiProgramCheckError> OsShell::guiProgramInvocation(const QString& commandLine, const QString& workingDir)
{
	// Variable expansion, redirection, pipes, chaining and grouping all need cmd; % expands even inside quotes
	bool inQuotes = false;
	for (const QChar c : commandLine)
	{
		if (c == '"')
			inQuotes = !inQuotes;
		else if (c == '%' || (!inQuotes && QStringView{ u"&|<>^()" }.contains(c)))
			return std::nullopt;
	}

	// The program ends at its closing quote, or at the first space when unquoted
	const QString line = commandLine.trimmed();
	const bool quoted = line.startsWith('"');
	const qsizetype programEnd = quoted ? line.indexOf('"', 1) : line.indexOf(' ');
	if (quoted && programEnd < 0)
		return std::nullopt;

	const QString program = quoted ? line.mid(1, programEnd - 1) : line.left(programEnd);
	if (program.isEmpty())
		return std::nullopt;

	// Not found also covers cmd built-ins
	QString programPath = resolvedProgramPath(program, workingDir);
	if (programPath.isEmpty())
		return std::nullopt;

	QString arguments = programEnd < 0 ? QString{} : line.mid(programEnd + 1).trimmed();
	// A bare cmd is interactive: through the shell it reads the null stdin and exits at once
	if (arguments.isEmpty() && QFileInfo{ programPath }.fileName().compare(QStringLiteral("cmd.exe"), Qt::CaseInsensitive) == 0)
	{
		// cmd does not support a UNC current directory: pushd maps a temporary drive letter for it
		if (const QString nativeWorkingDir = toNativeSeparators(workingDir); nativeWorkingDir.startsWith(QStringLiteral("\\\\")))
			return ProgramInvocation{ .programPath = std::move(programPath), .arguments = QStringLiteral("/k pushd \"") % nativeWorkingDir % '"', .workingDir = {} };

		return ProgramInvocation{ .programPath = std::move(programPath), .arguments = {}, .workingDir = workingDir };
	}

	// 0 for a non-executable file or a failed query; otherwise the high word is the Windows version a GUI program targets, 0 for a console program or batch file
	const DWORD_PTR exeType = ::SHGetFileInfoW(reinterpret_cast<const wchar_t*>(toNativeSeparators(programPath).utf16()), 0, nullptr, 0, SHGFI_EXETYPE);
	if (exeType == 0)
		return std::unexpected{ GuiProgramCheckError::ExecutableTypeUnknown };
	if (HIWORD(exeType) == 0)
		return std::nullopt;

	return ProgramInvocation{ .programPath = std::move(programPath), .arguments = std::move(arguments), .workingDir = workingDir };
}
#else
// sh returns at once for a program started with & or through open.
std::expected<std::optional<OsShell::ProgramInvocation>, OsShell::GuiProgramCheckError> OsShell::guiProgramInvocation(const QString& /*commandLine*/, const QString& /*workingDir*/)
{
	return std::unexpected{ GuiProgramCheckError::UnsupportedPlatform };
}
#endif

#ifdef _WIN32

static QString launchErrorText(const DWORD errorCode)
{
	const QString message = QString::fromStdString(ErrorStringFromErrorCode(errorCode));
	return !message.isEmpty() ? message : QStringLiteral("Error code %1").arg(errorCode);
}

std::expected<void, QString> OsShell::runExecutable(const QString& command, const QString& arguments, const QString& workingDir)
{
	return runExe(command, arguments, workingDir, false);
}

std::expected<void, QString> OsShell::runExe(const QString& command, const QString& arguments, const QString& workingDir, bool asAdmin)
{
	const QString workingDirNative = toNativeSeparators(workingDir);
	// The extended-length form: ShellExecuteExW accepts it, and without it MAX_PATH caps what can be launched.
	// A bare program name is left alone, so resolution through PATH still works.
	const thin_io::windows_path_buffer commandNative{ reinterpret_cast<const wchar_t*>(command.utf16()) };
	if (!commandNative)
		return std::unexpected{ launchErrorText(commandNative.error_code()) };

	SHELLEXECUTEINFOW shExecInfo;
	::memset(&shExecInfo, 0, sizeof(shExecInfo));

	shExecInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
	shExecInfo.fMask = SEE_MASK_FLAG_NO_UI;
	shExecInfo.hwnd = nullptr;
	shExecInfo.lpVerb = asAdmin ? L"runas" : L"open";
	shExecInfo.lpFile = commandNative.c_str();
	shExecInfo.lpParameters = arguments.isEmpty() ? nullptr : reinterpret_cast<const WCHAR*>(arguments.utf16());
	shExecInfo.lpDirectory = workingDirNative.isEmpty() ? nullptr : reinterpret_cast<const WCHAR*>(workingDirNative.utf16());
	shExecInfo.nShow = SW_SHOWNORMAL;
	shExecInfo.hInstApp = nullptr;

	if (ShellExecuteExW(&shExecInfo) == 0)
	{
		if (const DWORD error = ::GetLastError(); error != ERROR_CANCELLED) // Operation canceled by the user
		{
			QString errorText = launchErrorText(error);
			qInfo() << "ShellExecuteExW failed when trying to run" << QString::fromWCharArray(commandNative.c_str()) << "in" << workingDirNative;
			qInfo() << errorText;

			return std::unexpected{ std::move(errorText) };
		}
	}

	return {};
}
#else
std::expected<void, QString> OsShell::runExecutable(const QString & command, const QString & parameters, const QString & workingDir)
{
	QProcess process;
	process.setProgram(command);
	process.setArguments({ parameters });
	process.setWorkingDirectory(workingDir);
	if (!process.startDetached())
		return std::unexpected{ process.errorString() };

	return {};
}
#endif

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

namespace {

class CItemIdListReleaser {
public:
	explicit CItemIdListReleaser(PIDLIST_ABSOLUTE idList) : _idList(idList) {}
	~CItemIdListReleaser() { if (_idList) CoTaskMemFree(_idList); }
private:
	PIDLIST_ABSOLUTE _idList;
};

class CItemIdArrayReleaser {
public:
	explicit CItemIdArrayReleaser(const std::vector<PIDLIST_ABSOLUTE>& idArray) : _array(idArray) {}
	~CItemIdArrayReleaser() {
		for (PIDLIST_ABSOLUTE item : _array)
			CoTaskMemFree(item);
	}

	CItemIdArrayReleaser& operator=(const CItemIdArrayReleaser&) = delete;
private:
	const std::vector<PIDLIST_ABSOLUTE>& _array;
};

} // namespace

static bool prepareContextMenuForObjects(std::vector<std::wstring> objects, void* parentWindow, HMENU& hmenu, ComPtr<IContextMenu>& imenu);

// Pos must be global

bool OsShell::openShellContextMenuForObjects(const std::vector<std::wstring>& objects, int xPos, int yPos, void * parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	ComPtr<IContextMenu> imenu;
	HMENU hMenu = nullptr;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	const int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, reinterpret_cast<HWND>(parentWindow), nullptr);
	if (iCmd > 0)
	{
		CMINVOKECOMMANDINFO info;
		::memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		info.hwnd = reinterpret_cast<HWND>(parentWindow);
		info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
		info.nShow = SW_SHOWNORMAL;
		imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
	}

	DestroyMenu(hMenu);

	return true;
}

bool OsShell::copyObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	ComPtr<IContextMenu> imenu;
	HMENU hMenu = nullptr;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CMINVOKECOMMANDINFO info;
	::memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.hwnd = reinterpret_cast<HWND>(parentWindow);
	info.lpVerb = "Copy";
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

bool OsShell::cutObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	ComPtr<IContextMenu> imenu;
	HMENU hMenu = nullptr;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CMINVOKECOMMANDINFO info;
	::memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.hwnd = reinterpret_cast<HWND>(parentWindow);
	info.lpVerb = "Cut";
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO) &info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

bool OsShell::pasteFilesAndFoldersFromClipboard(std::wstring destFolder, void * parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	ComPtr<IContextMenu> imenu;
	HMENU hMenu = nullptr;
	if (!prepareContextMenuForObjects(std::vector<std::wstring>{std::move(destFolder)}, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CMINVOKECOMMANDINFO info;
	::memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.hwnd = reinterpret_cast<HWND>(parentWindow);
	info.lpVerb = "Paste";
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO) &info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

std::wstring OsShell::toolTip(std::wstring itemPath)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	std::replace(itemPath.begin(), itemPath.end(), '/', '\\');
	std::wstring tipString;
	PIDLIST_ABSOLUTE id = nullptr;
	HRESULT result = SHParseDisplayName(itemPath.c_str(), nullptr, &id, 0, nullptr);
	if (!SUCCEEDED(result) || !id)
		return tipString;
	CItemIdListReleaser idReleaser (id);

	PCIDLIST_ABSOLUTE child = nullptr;
	ComPtr<IShellFolder> ifolder;
	result = SHBindToParent(id, IID_IShellFolder, reinterpret_cast<void**>(ifolder.GetAddressOf()), &child);
	if (!SUCCEEDED(result) || !child)
		return tipString;

	ComPtr<IQueryInfo> iQueryInfo;
	if (SUCCEEDED(ifolder->GetUIObjectOf(nullptr, 1, &child, IID_IQueryInfo, nullptr, reinterpret_cast<void**>(iQueryInfo.GetAddressOf()))) && iQueryInfo)
	{
		LPWSTR lpszTip = nullptr;
		if (SUCCEEDED(iQueryInfo->GetInfoTip(0, &lpszTip)) && lpszTip)
		{
			tipString = lpszTip;
			CoTaskMemFree(lpszTip);
		}
	}

	std::replace(tipString.begin(), tipString.end(), '\r', '\n');
	return tipString;
}

bool OsShell::deleteItems(const std::vector<std::wstring>& items, bool moveToTrash, void * parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	assert_r(parentWindow);
	std::vector<PIDLIST_ABSOLUTE> idLists;

	EXEC_ON_SCOPE_EXIT([&idLists] {
		for (auto& pid : idLists)
			ILFree(pid);

		idLists.clear();
	});

	for (const auto& path: items)
	{
		PIDLIST_ABSOLUTE idl = ILCreateFromPathW(path.c_str());
		if (!idl)
		{
			qInfo() << "ILCreateFromPathW" << "failed for path" << QString::fromStdWString(path);
			return false;
		}
		idLists.push_back(idl);
		assert_r(idLists.back());
	}

	ComPtr<IShellItemArray> iArray;
	HRESULT result = SHCreateShellItemArrayFromIDLists((UINT)idLists.size(), (PCIDLIST_ABSOLUTE_ARRAY)idLists.data(), iArray.GetAddressOf());

	if (!SUCCEEDED(result) || !iArray)
	{
		qInfo() << "SHCreateShellItemArrayFromIDLists failed";
		return false;
	}

	ComPtr<IFileOperation> iOperation;
	result = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation, reinterpret_cast<void**>(iOperation.GetAddressOf()));
	if (!SUCCEEDED(result) || !iOperation)
	{
		qInfo() << "CoCreateInstance(CLSID_FileOperation, 0, CLSCTX_ALL, IID_IFileOperation, (void**)&iOperation) failed";
		return false;
	}

	result = iOperation->DeleteItems(iArray.Get());
	if (!SUCCEEDED(result))
	{
		qInfo() << "DeleteItems failed";
	}
	else
	{
		if (moveToTrash)
		{
			result = iOperation->SetOperationFlags(FOF_ALLOWUNDO);
		}
		else
			result = iOperation->SetOperationFlags(FOF_WANTNUKEWARNING);

		if (!SUCCEEDED(result))
			qInfo() << "SetOperationFlags failed";

		result = iOperation->SetOwnerWindow(reinterpret_cast<HWND>(parentWindow));
		if (!SUCCEEDED(result))
			qInfo() << "SetOwnerWindow failed";

		// When you close the system dialog with the title bar X rather than the cancel button inside the dialog
		static constexpr HRESULT UserCancelledAltError = 0x800704c7L;

		result = iOperation->PerformOperations();
		if (!SUCCEEDED(result) && result != COPYENGINE_E_USER_CANCELLED && result != UserCancelledAltError)
		{
			qInfo().nospace() << "PerformOperations failed with 0x" << Qt::hex << (uint32_t)result;
			if (result == COPYENGINE_E_REQUIRES_ELEVATION)
				qInfo() << "Elevation required";
		}
		else
			result = S_OK;
	}

	return SUCCEEDED(result);
}

bool OsShell::recycleBinContextMenu(int xPos, int yPos, void *parentWindow)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	PIDLIST_ABSOLUTE idlist = nullptr;
	if (!SUCCEEDED(SHGetFolderLocation(nullptr, CSIDL_BITBUCKET, nullptr, 0, &idlist)))
		return false;

	CItemIdListReleaser idlistReleaser(idlist); // 'list' below points into it, so it must outlive the menu setup

	ComPtr<IShellFolder> iFolder;
	PCIDLIST_ABSOLUTE list = nullptr;
	HRESULT result = SHBindToParent(idlist, IID_IShellFolder, reinterpret_cast<void**>(iFolder.GetAddressOf()), &list);
	if (!SUCCEEDED(result) || !list || !iFolder)
		return false;

	ComPtr<IContextMenu> imenu;
	result = iFolder->GetUIObjectOf(reinterpret_cast<HWND>(parentWindow), 1u, &list, IID_IContextMenu, nullptr, reinterpret_cast<void**>(imenu.GetAddressOf()));
	if (!SUCCEEDED(result) || !imenu)
		return false;

	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return false;
	if (SUCCEEDED(imenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL)))
	{
		int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, reinterpret_cast<HWND>(parentWindow), nullptr);
		if (iCmd > 0)
		{
			CMINVOKECOMMANDINFOEX info;
			::memset(&info, 0, sizeof(info));

			info.cbSize = sizeof(info);
			info.fMask = CMIC_MASK_UNICODE;
			info.hwnd = reinterpret_cast<HWND>(parentWindow);
			info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
			info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
			info.nShow = SW_SHOWNORMAL;
			imenu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
		}
	}
	DestroyMenu(hMenu);
	return true;
}

static bool prepareContextMenuForObjects(std::vector<std::wstring> objects, void * parentWindow, HMENU& hmenu, ComPtr<IContextMenu>& imenu)
{
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	if (objects.empty())
		return false;

	std::vector<PIDLIST_ABSOLUTE> ids;
	std::vector<PCIDLIST_ABSOLUTE> relativeIds;
	ComPtr<IShellFolder> ifolder;
	for (size_t i = 0, nItems = objects.size(); i < nItems; ++i)
	{
		auto& item = objects[i];
		std::replace(item.begin(), item.end(), '/', '\\');
		//item.pop_back(); // TODO: ???
		ids.emplace_back(nullptr);
		HRESULT result = SHParseDisplayName(item.c_str(), nullptr, &ids.back(), 0, nullptr); // TODO: avoid c_str() somehow?
		if (!SUCCEEDED(result) || !ids.back())
		{
			ids.pop_back();
			continue;
		}

		relativeIds.emplace_back(nullptr);
		result = SHBindToParent(ids.back(), IID_IShellFolder, reinterpret_cast<void**>(ifolder.ReleaseAndGetAddressOf()), &relativeIds.back());
		if (!SUCCEEDED(result) || !relativeIds.back())
			relativeIds.pop_back();
		else if (i < nItems - 1)
			ifolder.Reset();
	}

	CItemIdArrayReleaser arrayReleaser(ids);

	assert_r(parentWindow);
	assert_and_return_message_r(ifolder, "Error getting ifolder", false);
	assert_and_return_message_r(!relativeIds.empty(), "RelativeIds is empty", false);

	const HRESULT result = ifolder->GetUIObjectOf(
		reinterpret_cast<HWND>(parentWindow),
		(UINT)relativeIds.size(),
		reinterpret_cast<const ITEMIDLIST **>(relativeIds.data()),
		IID_IContextMenu,
		nullptr,
		reinterpret_cast<void**>(imenu.ReleaseAndGetAddressOf())
	);

	if (!SUCCEEDED(result) || !imenu)
		return false;

	hmenu = CreatePopupMenu();
	if (!hmenu)
		return false;
	return (SUCCEEDED(imenu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL)));
}

#elif defined __linux__ || __FreeBSD__

bool OsShell::openShellContextMenuForObjects(const std::vector<std::wstring>& /*objects*/, int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return false;
}

std::wstring OsShell::toolTip(std::wstring /*itemPath*/)
{
	return std::wstring();
}

bool OsShell::recycleBinContextMenu(int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return true;
}

#elif defined __APPLE__

bool OsShell::openShellContextMenuForObjects(const std::vector<std::wstring>& /*objects*/, int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return false;
}

std::wstring OsShell::toolTip(std::wstring /*itemPath*/)
{
	return std::wstring();
}

bool OsShell::recycleBinContextMenu(int /*xPos*/, int /*yPos*/, void */*parentWindow*/)
{
	return true;
}

#else
#error unsupported platform
#endif

bool OsShell::isInPath(const QString& fileName)
{
#ifdef _WIN32
	static constexpr char pathSeparator = ';';
#else
	static constexpr char pathSeparator = ':';
#endif
	const QString path = qEnvironmentVariable("PATH");
	const QStringList pathEntries = path.split(pathSeparator, Qt::SkipEmptyParts);
	for (const QString& pathEntry : pathEntries)
	{
		if (QFile::exists(pathEntry + '/' + fileName))
			return true;
	}

	return false;
}