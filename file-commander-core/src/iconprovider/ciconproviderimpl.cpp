#include "ciconproviderimpl.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
DISABLE_COMPILER_WARNINGS
#include <QtWin>
RESTORE_COMPILER_WARNINGS

#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

QIcon CIconProviderImpl::iconFor(const CFileSystemObject& object)
{
	QIcon icon;
	SHFILEINFO info;
	memset(&info, 0, sizeof(info));
	SHGetFileInfoW((WCHAR*)object.fullAbsolutePath().replace('/', '\\').utf16(), object.isDir() ? FILE_ATTRIBUTE_DIRECTORY : 0, &info, sizeof(SHFILEINFO),
				   SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON | (_showOverlayIcons ? SHGFI_ADDOVERLAYS : 0));

	if (info.hIcon)
	{
		icon = QIcon(QtWin::fromHICON(info.hIcon));
		DestroyIcon(info.hIcon);
	}

	auto sizes = icon.availableSizes();
	return icon;
}

void CIconProviderImpl::settingsChanged()
{
	_showOverlayIcons = CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();
}

#else
QIcon CIconProviderImpl::iconFor(const CFileSystemObject &object)
{
#ifndef CFILESYSTEMOBJECT_TEST // TODO: Remove this ugly hack
	return _provider.icon(object.qFileInfo());
#else
	return QIcon();
#endif
}

void CIconProviderImpl::settingsChanged()
{
	_showOverlayIcons = CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();

	const auto oldOptions = _provider.options();
	const auto newOptions = _showOverlayIcons ? QFlags<QFileIconProvider::Option>() : QFileIconProvider::DontUseCustomDirectoryIcons;
	if (oldOptions != newOptions)
		_provider.setOptions(newOptions);
}
#endif
