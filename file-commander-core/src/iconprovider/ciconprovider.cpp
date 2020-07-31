#include "ciconprovider.h"
#include "cfilesystemobject.h"
#include "ciconproviderimpl.h"
#include "fasthash.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <memory>

std::unique_ptr<CIconProvider> CIconProvider::_instance;

const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject &object)
{
	if (!_instance)
	{
		_instance = std::unique_ptr<CIconProvider>(new CIconProvider);
		settingsChanged();
	}

	return _instance->iconFor(object);
}

void CIconProvider::settingsChanged()
{
	if (_instance && _instance->_provider)
	{
		_instance->_provider->settingsChanged();
		_instance->_iconByItsHash.clear();
		_instance->_iconHashForObjectHash.clear();
	}
}

CIconProvider::CIconProvider() : _provider(new CIconProviderImpl)
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

const QIcon& CIconProvider::iconFor(const CFileSystemObject& object)
{
	const qulonglong objectHash = hash(object);
	const auto iconHashIterator = _iconHashForObjectHash.find(objectHash);
	if (iconHashIterator == _iconHashForObjectHash.end())
	{
		const QIcon icon = _provider->iconFor(object);
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
