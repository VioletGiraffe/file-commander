#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QHash>
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <unordered_map>

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
	const QIcon& iconFast(const CFileSystemObject& object);

private:
	static std::unique_ptr<CIconProvider> _instance;

	std::unordered_map<qint64, QIcon> _iconByKey;
	QHash<QString, QIcon> _iconByExtension;

	QHash<qulonglong, qint64> _iconKeyByObjectHash;

	std::unique_ptr<CIconProviderImpl> _provider;
};
