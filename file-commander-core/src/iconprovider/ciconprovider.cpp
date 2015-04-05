#include "ciconprovider.h"
#include "../cfilesystemobject.h"
#include <assert.h>

std::unique_ptr<CIconProvider> CIconProvider::_impl = std::unique_ptr<CIconProvider>(new CIconProvider);

const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject &object)
{
	static const QIcon dummy;
	return _impl ? _impl->iconFor(object) : dummy;
}

CIconProvider::CIconProvider()
{
	_provider.setOptions(QFileIconProvider::DontUseCustomDirectoryIcons);
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
		const QIcon icon = _provider.icon(object.absoluteFilePath());
		assert(!icon.isNull());
		const quint64 iconHash = icon.cacheKey();
		if (_iconCache.count(iconHash) == 0)
			_iconCache[iconHash] = icon;

		_iconForObject[objectHash] = iconHash;
		return _iconCache[iconHash];
	}

	return _iconCache[_iconForObject[objectHash]];
}
