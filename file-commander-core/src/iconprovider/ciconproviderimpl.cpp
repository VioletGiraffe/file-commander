#include "ciconproviderimpl.h"
#include "cfilesystemobject.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "system/win_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
DISABLE_COMPILER_WARNINGS
#include <QtWin>
RESTORE_COMPILER_WARNINGS

#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

inline wchar_t* appendToString(wchar_t* buffer, const wchar_t* what, size_t whatLengthInCharacters = 0)
{
	if (whatLengthInCharacters == 0)
		whatLengthInCharacters = wcslen(what);

	memcpy(buffer, what, whatLengthInCharacters * sizeof(wchar_t));
	return buffer + whatLengthInCharacters;
}

inline wchar_t* appendToString(wchar_t* buffer, const QString& what)
{
	const auto written = what.toWCharArray(buffer);
	return buffer + written;
}

QIcon CIconProviderImpl::iconFor(const CFileSystemObject& object, const bool guessIconByFileExtension) noexcept
{
	SHFILEINFOW info;
	const UINT flags = SHGFI_ICON | SHGFI_SMALLICON | (_showOverlayIcons ? SHGFI_ADDOVERLAYS : 0) | (guessIconByFileExtension ? SHGFI_USEFILEATTRIBUTES : 0);

	DWORD_PTR result = 0;

	if (!guessIconByFileExtension)
	{
		WCHAR pathStringBuffer[32768];
		// TODO: UNC path?
		// TODO: create a helper function for this in cpputils
		const auto length = object.fullAbsolutePath().toWCharArray(pathStringBuffer);
		pathStringBuffer[length] = 0;

		std::replace(pathStringBuffer, pathStringBuffer + length, L'/', L'\\');

		if (object.isDir())
			result = SHGetFileInfoW(pathStringBuffer, FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info), flags);
		else
			result = SHGetFileInfoW(pathStringBuffer, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags);
	}
	else // Fast method that can't handle special icons for certain specific files (rather than certain file types)
	{
		if (object.isDir())
			result = SHGetFileInfoW(L"a", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info), flags);
		else
		{
			WCHAR nameBuffer[32768];
			auto* itemName = appendToString(nameBuffer, L"x", 1);
			itemName = appendToString(itemName, object.extension());
			itemName = 0; // Null-terminator

			result = SHGetFileInfoW(nameBuffer, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags);
		}
	}

	if (result == 0 || !info.hIcon)
	{
		return {};
	}

	QIcon icon = QIcon(QtWin::fromHICON(info.hIcon));
	DestroyIcon(info.hIcon);
	return icon;
}

void CIconProviderImpl::setShowOverlayIcons(const bool show) noexcept
{
	_showOverlayIcons = show;
}

#else // ! _WIN32

QIcon CIconProviderImpl::iconFor(const CFileSystemObject &object, const bool /*guessIconByFileExtension*/) noexcept
{
#ifndef CFILESYSTEMOBJECT_TEST // TODO: Remove this ugly hack
	return _provider.icon(object.qFileInfo());
#else
	return {};
#endif
}

void CIconProviderImpl::setShowOverlayIcons(const bool show) noexcept
{
	const auto oldOptions = _provider.options();
	auto newOptions = oldOptions;
	if (show)
		newOptions |= QFileIconProvider::DontUseCustomDirectoryIcons;
	else
		newOptions &= (~QFileIconProvider::DontUseCustomDirectoryIcons);

	if (oldOptions != newOptions)
		_provider.setOptions(newOptions);
}

#endif
