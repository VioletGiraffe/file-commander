#include "ciconprovider.h"
#include "cfilesystemobject.h"
#include "ciconproviderimpl.h"

#include "assert/advanced_assert.h"
#include "hash/wheathash.hpp"

#include "settings/csettings.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

// TODO: refactor this ugly singleton with unclear lifetime!
std::unique_ptr<CIconProvider> CIconProvider::_instance { new CIconProvider }; // Cannot use make_unique because CIconProvider's constructor is private

// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject &object, bool guessIconByFileExtension)
{
	return _instance->iconFor(object, guessIconByFileExtension);
}

void CIconProvider::settingsChanged()
{
	if (_instance && _instance->_provider)
	{
		const bool showOverlayIcons = CSettings{}.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool();
		_instance->_provider->setShowOverlayIcons(showOverlayIcons);

		_instance->_iconByItsHash.clear();
		_instance->_iconHashForObjectHash.clear();
	}
}

CIconProvider::CIconProvider() : _provider{std::make_unique<CIconProviderImpl>()}
{
	settingsChanged();
}

// This complicated hashing function should have addressed #97, but it doesn't actually fix it
// Temporarily disabled for optimization
//inline static qulonglong hash(const CFileSystemObject& object)
//{
//	const auto properties = object.properties();
//	// Use the original hash as the seed and mix it with other attributes that uniquely identify this object
//	uint64_t hash = wheathash64v(properties.modificationDate, properties.hash);
//	hash = wheathash64v(properties.creationDate, hash);
//	hash = wheathash64v(properties.size, hash);
//	hash = wheathash64v(properties.type, hash);

//	return hash;
//}

// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
const QIcon& CIconProvider::iconFor(const CFileSystemObject& object, bool guessIconByFileExtension)
{
	const qulonglong objectHash = object.hash();
	const auto iconHashIterator = _iconHashForObjectHash.find(objectHash);
	if (iconHashIterator == _iconHashForObjectHash.end())
	{
		QIcon icon = _provider->iconFor(object, guessIconByFileExtension);
		if (icon.isNull())
		{
			if (!object.isSymLink())
				assert_unconditional_r("Icon for " + object.fullAbsolutePath().toStdString() + " is null.");

			static const QIcon nullIcon;
			return nullIcon;
		}

		if (_iconByItsHash.size() > 10000)
		{
			_iconByItsHash.clear();
			_iconHashForObjectHash.clear();
		}

		const auto iconHash = icon.cacheKey();
		const auto iconInContainer = _iconByItsHash.emplace(iconHash, std::move(icon)).first;
		_iconHashForObjectHash[objectHash] = iconHash;

		return iconInContainer->second;
	}
	else
		return _iconByItsHash[iconHashIterator->second];
}
