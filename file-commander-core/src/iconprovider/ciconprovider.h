#pragma once

#include "compiler/compiler_warnings_control.h"
#include "detail/hashmap_helpers.h"

#include "ankerl/unordered_dense.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

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
	const QIcon& iconFast(const CFileSystemObject& object);

private:
	static std::unique_ptr<CIconProvider> _instance;

	ankerl::unordered_dense::segmented_map<qint64, QIcon, NullHashExtraMixing> _iconByKey;
	ankerl::unordered_dense::segmented_map<QString, QIcon, QStringHash> _iconByExtension;

	ankerl::unordered_dense::map<qulonglong, qint64, NullHash> _iconKeyByObjectHash;

	std::unique_ptr<CIconProviderImpl> _provider;
};
