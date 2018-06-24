#include "ciconproviderimpl.h"

#ifdef _WIN32

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
