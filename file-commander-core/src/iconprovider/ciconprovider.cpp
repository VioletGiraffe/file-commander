#include "ciconprovider.h"
#include "../cfilesystemobject.h"
#include "ciconproviderimpl.h"

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
	const auto hashData = QCryptographicHash::hash(
		QByteArray::fromRawData((const char*)&properties.hash, sizeof(properties.hash)) +
			QByteArray::fromRawData((const char*)&properties.modificationDate, sizeof(properties.modificationDate)) +
			QByteArray::fromRawData((const char*)&properties.type, sizeof(properties.type)),
		QCryptographicHash::Md5);

	return *(qulonglong*)(hashData.data()) ^ *(qulonglong*)(hashData.data()+8);
}

const QIcon& CIconProvider::iconFor(const CFileSystemObject& object)
{
	const qulonglong objectHash = hash(object);
	if (_iconForObject.count(objectHash) == 0)
	{
		const QIcon icon = _provider->iconFor(object);
		Q_ASSERT(!icon.isNull());

		QCryptographicHash qCryptoHash(QCryptographicHash::Md5);

		const auto qimage = icon.pixmap(icon.availableSizes().front()).toImage();
		qCryptoHash.addData((const char*)qimage.constBits(), qimage.bytesPerLine() * qimage.height());
		const auto result = qCryptoHash.result();
		const qulonglong iconHash = *(qulonglong*)(result.data()) ^ *(qulonglong*)(result.data()+8);

		if (_iconCache.count(iconHash) == 0)
		{
			if (_iconCache.size() > 300)
			{
				_iconCache.clear();
				_iconForObject.clear();
			}
			_iconCache[iconHash] = icon;
		}

		_iconForObject[objectHash] = iconHash;
		return icon;
	}

	return _iconCache[_iconForObject[objectHash]];
}
