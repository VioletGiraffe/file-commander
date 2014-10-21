#include "ciconprovider.h"
#include "../cfilesystemobject.h"
#include <assert.h>

CIconProvider::CIconProvider()
{
}

const QIcon& CIconProvider::iconForFilesystemObject(const CFileSystemObject& object)
{
	if (_iconForObject.count(object.properties().hash) == 0)
	{
		const QIcon icon = QFileIconProvider().icon(object.absoluteFilePath());
		assert(!icon.isNull());
		const quint64 iconHash = icon.cacheKey();
		if (_iconCache.count(iconHash) == 0)
			_iconCache[iconHash] = icon;

		_iconForObject[object.properties().hash] = iconHash;
		return _iconCache[iconHash];
	}

	return _iconCache[_iconForObject[object.properties().hash]];
}

std::unordered_map<qulonglong, quint64> CIconProvider::_iconForObject;
std::unordered_map<quint64, QIcon> CIconProvider::_iconCache;
