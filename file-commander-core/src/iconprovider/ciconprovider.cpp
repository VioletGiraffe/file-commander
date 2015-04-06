#include "ciconprovider.h"
#include "../cfilesystemobject.h"
#include "settings.h"
#include "settings/csettings.h"

#include <assert.h>

std::unique_ptr<CIconProvider> CIconProvider::_impl;

const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject &object)
{
	if (!_impl)
	{
		_impl = std::unique_ptr<CIconProvider>(new CIconProvider);
		settingsChanged();
	}

	return _impl->iconFor(object);
}

void CIconProvider::settingsChanged()
{
	if (_impl)
	{
		const auto oldOptions = _impl->_provider.options();
		const auto newOptions = CSettings::instance()->value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool() ? QFlags<QFileIconProvider::Option>() : QFileIconProvider::DontUseCustomDirectoryIcons;
		if (oldOptions != newOptions)
		{
			_impl->_provider.setOptions(newOptions);
			_impl->_iconCache.clear();
			_impl->_iconForObject.clear();
		}
	}
}

CIconProvider::CIconProvider()
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
	const qulonglong objectHash = hash(object);	if (_iconForObject.count(objectHash) == 0)
	{
		const QIcon icon = _provider.icon(object.absoluteFilePath());
		assert(!icon.isNull());
		QCryptographicHash qCryptoHash(QCryptographicHash::Md5);

		const auto qimage = icon.pixmap(icon.availableSizes().front()).toImage();
		qCryptoHash.addData((const char*)qimage.constBits(), qimage.bytesPerLine() * qimage.height());
		const auto result = qCryptoHash.result();
		const qulonglong iconHash = *(qulonglong*)(result.data()) ^ *(qulonglong*)(result.data()+8);
		if (_iconCache.count(iconHash) == 0)
			_iconCache[iconHash] = icon;

		_iconForObject[objectHash] = iconHash;
		return _iconCache[iconHash];
	}

	return _iconCache[_iconForObject[objectHash]];
}
