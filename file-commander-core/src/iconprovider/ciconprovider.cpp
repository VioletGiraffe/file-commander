#include "ciconprovider.h"
#include "ciconproviderimpl.h"

#include "settings/csettings.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
QIcon CIconProvider::iconForFilesystemObject(const CFileSystemObject &object, bool guessIconByFileExtension)
{
	return get()._provider->iconFor(object, guessIconByFileExtension);
}

void CIconProvider::settingsChanged()
{
	get().onSettingsChanged();
}

CIconProvider::CIconProvider() : _provider{std::make_unique<CIconProviderImpl>()}
{
	onSettingsChanged();
}

void CIconProvider::onSettingsChanged()
{
	if (!_provider)
		return;

	const bool showOverlayIcons = CSettings{}.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();
	_provider->setShowOverlayIcons(showOverlayIcons);
}

CIconProvider& CIconProvider::get()
{
	static CIconProvider provider;
	return provider;
}

