#include "ciconprovider.h"
#include "../cfilesystemobject.h"
#include <assert.h>

std::shared_ptr<CIconProvider> CIconProvider::_impl = std::shared_ptr<CIconProvider>(new CIconProvider);

const QIcon &CIconProvider::iconForFilesystemObject(const CFileSystemObject &object)
{
	static const QIcon dummy;
	return _impl ? _impl->iconFor(object) : dummy;
}

CIconProvider::CIconProvider()
{
}

const QIcon& CIconProvider::iconFor(const CFileSystemObject& object)
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
