#include "ciconprovider.h"
#include "cfilesystemobject.h"
#include "ciconproviderimpl.h"
#include "fasthash.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

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
		_instance->_iconCache.clear();
		_instance->_iconForObject.clear();
	}
}

CIconProvider::CIconProvider() : _provider(new CIconProviderImpl)
{
}

inline static qulonglong hash(const CFileSystemObject& object)
{
	const auto properties = object.properties();
	const auto hashData =
		QByteArray::fromRawData((const char*) &properties.modificationDate, sizeof(properties.modificationDate)) +
		QByteArray::fromRawData((const char*) &properties.creationDate, sizeof(properties.creationDate)) +
		QByteArray::fromRawData((const char*) &properties.size, sizeof(properties.size)) +
		QByteArray::fromRawData((const char*) &properties.type, sizeof(properties.type));

	return fasthash64(hashData.constData(), hashData.size(), 0) ^ (uint64_t) properties.hash;
}

const QIcon& CIconProvider::iconFor(const CFileSystemObject& object)
{
	const qulonglong objectHash = hash(object);
	if (_iconForObject.count(objectHash) == 0)
	{
		const QIcon icon = _provider->iconFor(object);
		assert_r(!icon.isNull());

		const auto qimage = icon.pixmap(icon.availableSizes().front()).toImage();
		const qulonglong iconHash = fasthash64((const char*) qimage.constBits(), qimage.bytesPerLine() * qimage.height(), 0);

		if (_iconCache.size() > 300)
		{
			_iconCache.clear();
			_iconForObject.clear();
		}

		const auto iconInContainer = _iconCache.insert(std::make_pair(iconHash, icon)).first;
		_iconForObject[objectHash] = iconHash;

		return iconInContainer->second;
	}

	return _iconCache[_iconForObject[objectHash]];
}
