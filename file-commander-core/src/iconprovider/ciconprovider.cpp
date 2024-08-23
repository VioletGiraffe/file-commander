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

		_instance->_iconByKey.clear();
		_instance->_iconKeyByObjectHash.clear();
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
	if (guessIconByFileExtension)
		return iconFast(object);

	const qulonglong objectHash = object.hash();
	const auto iconHashIterator = _iconKeyByObjectHash.find(objectHash);
	if (iconHashIterator != _iconKeyByObjectHash.end())
		return _iconByKey[iconHashIterator.value()];

	if (_iconKeyByObjectHash.size() > 500'000) [[unlikely]]
	{
		_iconKeyByObjectHash.clear();
	}

	QIcon icon = _provider->iconFor(object, false);
	const auto iconHash = icon.cacheKey();
	_iconKeyByObjectHash.emplace(objectHash, iconHash);

	const auto iconInContainer = _iconByKey.insert_or_assign(iconHash, std::move(icon));
	return iconInContainer.first->second;
}

const QIcon& CIconProvider::iconFast(const CFileSystemObject& object)
{
	static const QString folderUid = "@!$%#&?~";

	QString extension = object.isDir() ? folderUid : object.extension();
	const auto it = _iconByExtension.find(extension);
	if (it != _iconByExtension.end())
		return it.value();

	QIcon icon = _provider->iconFor(object, true);

	if (_iconByExtension.size() > 10000) [[unlikely]]
		_iconByExtension.clear();

	const auto iconInContainer = _iconByExtension.emplace(std::move(extension), std::move(icon));
	return iconInContainer.value();
}
