#pragma once

#include "compiler/compiler_warnings_control.h"

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
	static QIcon iconForFilesystemObject(const CFileSystemObject& object, bool guessIconByFileExtension);
	static void settingsChanged();

private:
	static CIconProvider& get();

	CIconProvider();
	void onSettingsChanged();

private:
	std::unique_ptr<CIconProviderImpl> _provider;
};
