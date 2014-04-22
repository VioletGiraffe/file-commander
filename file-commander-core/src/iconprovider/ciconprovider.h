#ifndef CICONPROVIDER_H
#define CICONPROVIDER_H

#include <unordered_map>
#include "QtCoreIncludes"

class CFileSystemObject;
class CIconProvider
{
public:
	CIconProvider();

	static const QIcon& iconForFilesystemObject (const CFileSystemObject& object);

private:
	static std::unordered_map<quint64, QIcon> _iconCache;
	static std::unordered_map<uint, quint64>  _iconForObject;
};

#endif // CICONPROVIDER_H
