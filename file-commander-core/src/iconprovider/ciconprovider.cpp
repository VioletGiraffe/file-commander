#include "ciconprovider.h"
#include "cfilesystemobject.h"
#include "ciconproviderimpl.h"
#include "fasthash.h" // TODO: move to cpputils/hash
#include "assert/advanced_assert.h"

#include "settings/csettings.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <memory>

// TODO: refactor this ugly singleton with unclear lifetime!
std::unique_ptr<CIconProvider> CIconProvider::_instance;

// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject &object, bool guessIconByFileExtension)
{
	if (!_instance)
	{
		_instance = std::unique_ptr<CIconProvider>{new CIconProvider}; // Cannot use make_unique because CIconProvider's constructor is private
		settingsChanged();
	}

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
}

inline static qulonglong hash(const CFileSystemObject& object)
{
	const auto properties = object.properties();
	const auto hashData =
		QByteArray::fromRawData(reinterpret_cast<const char*>(std::addressof(properties.modificationDate)), sizeof(properties.modificationDate)) +
		QByteArray::fromRawData(reinterpret_cast<const char*>(std::addressof(properties.creationDate)), sizeof(properties.creationDate)) +
		QByteArray::fromRawData(reinterpret_cast<const char*>(std::addressof(properties.size)), sizeof(properties.size)) +
		QByteArray::fromRawData(reinterpret_cast<const char*>(std::addressof(properties.type)), sizeof(properties.type));

	return fasthash64(hashData.constData(), hashData.size(), 0) ^ (uint64_t)properties.hash;
}

// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
const QIcon& CIconProvider::iconFor(const CFileSystemObject& object, bool guessIconByFileExtension)
{
	const qulonglong objectHash = hash(object);
	const auto iconHashIterator = _iconHashForObjectHash.find(objectHash);
	if (iconHashIterator == _iconHashForObjectHash.end())
	{
		const QIcon icon = _provider->iconFor(object, guessIconByFileExtension);
		if (icon.isNull())
		{
			if (!object.isSymLink())
				assert_unconditional_r("Icon for " + object.fullAbsolutePath().toStdString() + " is null.");

			static const QIcon nullIcon;
			return nullIcon;
		}

		const auto qimage = icon.pixmap(icon.availableSizes().at(0)).toImage();
		const qulonglong iconHash = fasthash64(reinterpret_cast<const char*>(qimage.constBits()), qimage.bytesPerLine() * qimage.height(), 0);

		if (_iconByItsHash.size() > 300)
		{
			_iconByItsHash.clear();
			_iconHashForObjectHash.clear();
		}

		const auto iconInContainer = _iconByItsHash.insert(std::make_pair(iconHash, icon)).first;
		_iconHashForObjectHash[objectHash] = iconHash;

		return iconInContainer->second;
	}
	else
		return _iconByItsHash[iconHashIterator->second];
}
