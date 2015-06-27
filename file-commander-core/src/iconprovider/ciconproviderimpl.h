#pragma once

#include "QtCoreIncludes"
#include <QFileIconProvider>

#include "settings.h"
#include "settings/csettings.h"

#ifdef _WIN32

#include <shellapi.h>

class CIconProviderImpl
{
public:
	inline QIcon iconFor(const CFileSystemObject& object)
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

	inline void settingsChanged()
	{
		_showOverlayIcons = CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();
	}

private:
	bool _showOverlayIcons = false;
};

#else

class CIconProviderImpl
{
public:
	inline QIcon iconFor(const CFileSystemObject& object)
	{
		return _provider.icon(object.qFileInfo());
	}

	inline void settingsChanged()
	{
		_showOverlayIcons = CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();

		const auto oldOptions = _provider.options();
		const auto newOptions = _showOverlayIcons ? QFlags<QFileIconProvider::Option>() : QFileIconProvider::DontUseCustomDirectoryIcons;
		if (oldOptions != newOptions)
			_provider.setOptions(newOptions);
	}

private:
	bool _showOverlayIcons = false;
	QFileIconProvider _provider;
};

#endif

