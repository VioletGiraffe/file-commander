#include "cshell.h"

#include <assert.h>
#include <algorithm>

#ifdef _WIN32

#include <Shobjidl.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <windowsx.h>

class CItemIdListReleaser {
public:
	CItemIdListReleaser(ITEMIDLIST * idList) : _idList(idList) {}
	~CItemIdListReleaser() { if (_idList) CoTaskMemFree(_idList); }
private:
	ITEMIDLIST * _idList;
};

class CComInterfaceReleaser {
public:
	CComInterfaceReleaser(IUnknown * i) : _i(i) {}
	~CComInterfaceReleaser() { if (_i) _i->Release(); }
private:
	IUnknown * _i;
};

class CItemIdArrayReleaser {
public:
	CItemIdArrayReleaser(std::vector<ITEMIDLIST*> idArray) : _array(idArray) {}
	~CItemIdArrayReleaser() { for (auto it = _array.begin(); it != _array.end(); ++it) if (*it) CoTaskMemFree(*it); }
private:
	std::vector<ITEMIDLIST*> _array;
};


// Pos must be global
bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> objects, int xPos, int yPos, void * parentWindow)
{
	if (objects.empty())
		return false;

	std::vector<ITEMIDLIST*> ids;
	std::vector<LPCITEMIDLIST> relativeIds;
	IShellFolder * ifolder = 0;
	for (size_t i = 0; i < objects.size(); ++i)
	{
		std::replace(objects[i].begin(), objects[i].end(), '/', '\\');
		ids.push_back(0);
		HRESULT result = SHParseDisplayName(objects[i].c_str(), 0, &ids.back(), 0, 0);
		if (!SUCCEEDED(result) || !ids.back())
		{
			ids.pop_back();
			continue;
		}

		relativeIds.push_back(0);
		result = SHBindToParent(ids.back(), IID_IShellFolder, (void**)&ifolder, &relativeIds.back());
		if (!SUCCEEDED(result) || !relativeIds.back())
			relativeIds.pop_back();
		if (i < objects.size() - 1 && ifolder)
			ifolder->Release();
	}

	CItemIdArrayReleaser arrayReleaser (ids);

	assert (parentWindow);
	assert (ifolder);
	assert (!relativeIds.empty());

	IContextMenu * imenu = 0;
	HRESULT result = ifolder->GetUIObjectOf((HWND)parentWindow, (UINT)relativeIds.size(), (const ITEMIDLIST **)relativeIds.data(), IID_IContextMenu, 0, (void**)&imenu);
	if (!SUCCEEDED(result) || !imenu)
		return false;
	CComInterfaceReleaser menuReleaser(imenu);

	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return false;
	if (SUCCEEDED(imenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL)))
	{
		int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, (HWND)parentWindow, NULL);
		if (iCmd > 0)
		{
			CMINVOKECOMMANDINFOEX info = { 0 };
			info.cbSize = sizeof(info);
			info.fMask = CMIC_MASK_UNICODE;
			info.hwnd = (HWND)parentWindow;
			info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
			info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
			info.nShow = SW_SHOWNORMAL;
			imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
		}
	}
	DestroyMenu(hMenu);

	return true;
}

std::wstring CShell::toolTip(std::wstring itemPath)
{
	std::replace(itemPath.begin(), itemPath.end(), '/', '\\');
	std::wstring tipString;
	ITEMIDLIST * id = 0;
	HRESULT result = SHParseDisplayName(itemPath.c_str(), 0, &id, 0, 0);
	if (!SUCCEEDED(result) || !id)
		return tipString;
	CItemIdListReleaser idReleaser (id);

	LPCITEMIDLIST child = 0;
	IShellFolder * ifolder = 0;
	result = SHBindToParent(id, IID_IShellFolder, (void**)&ifolder, &child);
	if (!SUCCEEDED(result) || !child)
		return tipString;

	IQueryInfo* iQueryInfo = 0;
	if (SUCCEEDED(ifolder->GetUIObjectOf(NULL, 1, &child, IID_IQueryInfo, 0, (void**)&iQueryInfo)) && iQueryInfo)
	{
		LPWSTR lpszTip = 0;
		CComInterfaceReleaser releaser (iQueryInfo);
		if (SUCCEEDED(iQueryInfo->GetInfoTip(0, &lpszTip)) && lpszTip)
		{
			tipString = lpszTip;
			CoTaskMemFree(lpszTip);
		}
	}

	std::replace(tipString.begin(), tipString.end(), '\r', '\n');
	return tipString;
}

bool CShell::deleteItems(std::vector<std::wstring> items, bool moveToTrash, void * parentWindow)
{
	assert(parentWindow);
	std::vector<ITEMIDLIST*> idLists;
	for (auto& path: items)
	{
		__unaligned ITEMIDLIST* idl = ILCreateFromPathW(path.c_str());
		idLists.push_back(idl);
		assert(idLists.back());
	}

	IShellItemArray * iArray = 0;
	HRESULT result = SHCreateShellItemArrayFromIDLists((UINT)idLists.size(), (LPCITEMIDLIST*)idLists.data(), &iArray);

	// Freeing memory
	for (auto& pid: idLists)
		ILFree(pid);
	idLists.clear();

	if (!iArray || !SUCCEEDED(result))
		return false;

	IFileOperation * iOperation = 0;
	result = CoCreateInstance(CLSID_FileOperation, 0, CLSCTX_ALL, IID_IFileOperation, (void**)&iOperation);
	if (!SUCCEEDED(result) || !iOperation)
		return false;

	result = iOperation->DeleteItems(iArray);
	if (SUCCEEDED(result))
	{
		if (moveToTrash)
		{
			result = iOperation->SetOperationFlags(FOF_ALLOWUNDO);
		}
		else
			result = iOperation->SetOperationFlags(FOF_WANTNUKEWARNING);
		assert(SUCCEEDED(result));
		result = iOperation->SetOwnerWindow((HWND)parentWindow);
		assert(SUCCEEDED(result));
		result = iOperation->PerformOperations();
	}
	iOperation->Release();
	iArray->Release();
	return SUCCEEDED(result);
}

bool CShell::recycleBinContextMenu(int xPos, int yPos, void *parentWindow)
{
	PIDLIST_ABSOLUTE idlist = 0;
	if (!SUCCEEDED(SHGetFolderLocation(0, CSIDL_BITBUCKET, 0, 0, &idlist)))
		return false;

	IShellFolder * iFolder = 0;
	LPCITEMIDLIST list;
	HRESULT result = SHBindToParent(idlist, IID_IShellFolder, (void**)&iFolder, &list);
	if (!SUCCEEDED(result) || !list || !iFolder)
	{
		if (iFolder)
			iFolder->Release();
		return false;
	}

	IContextMenu * imenu = 0;
	result = iFolder->GetUIObjectOf((HWND)parentWindow, 1u, &list, IID_IContextMenu, 0, (void**)&imenu);
	CoTaskMemFree(idlist);
	if (!SUCCEEDED(result) || !imenu)
		return false;
	CComInterfaceReleaser menuReleaser(imenu);

	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return false;
	if (SUCCEEDED(imenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL)))
	{
		int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, (HWND)parentWindow, NULL);
		if (iCmd > 0)
		{
			CMINVOKECOMMANDINFOEX info = { 0 };
			info.cbSize = sizeof(info);
			info.fMask = CMIC_MASK_UNICODE;
			info.hwnd = (HWND)parentWindow;
			info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
			info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
			info.nShow = SW_SHOWNORMAL;
			imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
		}
	}
	DestroyMenu(hMenu);
	return true;
}

#elif defined __linux__

bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> /*objects*/, int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return false;
}

std::wstring CShell::toolTip(std::wstring /*itemPath*/)
{
	return std::wstring();
}

bool CShell::recycleBinContextMenu(int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return true;
}

#elif defined __APPLE__

bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> objects, int xPos, int yPos, void *parentWindow)
{
	return false;
}

std::wstring CShell::toolTip(std::wstring itemPath)
{
	return std::wstring();
}

bool CShell::recycleBinContextMenu(int xPos, int yPos, void *parentWindow)
{
	return true;
}

#else
#error unknown platform
#endif
