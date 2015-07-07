#ifndef CICONPROVIDER_H
#define CICONPROVIDER_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <unordered_map>
#include <memory>

class CFileSystemObject;
class CIconProviderImpl;

class CIconProviderImpl;

class CIconProvider
{
public:
	static const QIcon& iconForFilesystemObject(const CFileSystemObject& object);
	static void settingsChanged();

private:
	CIconProvider();
	const QIcon& iconFor(const CFileSystemObject& object);

private:
	static std::unique_ptr<CIconProvider> _instance;

	std::unordered_map<qulonglong, QIcon> _iconCache;
	std::unordered_map<qulonglong, qulonglong> _iconForObject;

	std::unique_ptr<CIconProviderImpl> _provider;
};

#endif // CICONPROVIDER_H
