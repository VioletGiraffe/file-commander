#pragma once

#ifndef _WIN32
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFileIconProvider>
RESTORE_COMPILER_WARNINGS
#endif

class CFileSystemObject;
class QIcon;

#ifdef _WIN32

class CIconProviderImpl
{
public:
	// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
	[[nodiscard]] QIcon iconFor(const CFileSystemObject& object, bool guessIconByFileExtension) const noexcept;
	void setShowOverlayIcons(bool show) noexcept;

private:
	bool _showOverlayIcons = false;
};

#else

class CIconProviderImpl
{
public:
	// guessIconByFileExtension is a less precise method, but much faster since it doesn't access the disk
	[[nodiscard]] QIcon iconFor(const CFileSystemObject& object, bool guessIconByFileExtension) noexcept;
	void setShowOverlayIcons(bool show) noexcept;

private:
	QFileIconProvider _provider;
};

#endif

