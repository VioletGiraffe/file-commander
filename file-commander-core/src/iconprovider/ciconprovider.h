#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <unordered_map>
#include <memory>

class CFileSystemObject;
class CIconProviderImpl;

class CIconProvider
{
public:
	// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
	static const QIcon& iconForFilesystemObject(const CFileSystemObject& object, bool guessIconByFileExtension);
	static void settingsChanged();

private:
	CIconProvider();
	const QIcon& iconFor(const CFileSystemObject& object, bool guessIconByFileExtension);

private:
	static std::unique_ptr<CIconProvider> _instance;

	std::unordered_map<qulonglong, QIcon> _iconByItsHash;
	std::unordered_map<qulonglong, qulonglong> _iconHashForObjectHash;

	std::unique_ptr<CIconProviderImpl> _provider;
};
