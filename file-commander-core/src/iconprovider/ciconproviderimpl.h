#pragma once

#ifndef _WIN32
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFileIconProvider>
RESTORE_COMPILER_WARNINGS
#endif

class CFileSystemObject;
class QIcon;
class QImage;
class QString;

#ifdef _WIN32

class CIconProviderImpl
{
public:
	// The icon the shell associates with the extension alone, or with folders when isDir. Never accesses the disk.
	[[nodiscard]] QImage genericIconImage(const QString& extension, bool isDir) const noexcept;
	// The object's own icon, which .exe, .ico and .lnk derive from their contents. Accesses the disk.
	// Requires the calling thread to be in a COM apartment.
	[[nodiscard]] QImage preciseIconImage(const CFileSystemObject& object) const noexcept;

	void setShowOverlayIcons(bool show) noexcept;

private:
	bool _showOverlayIcons = false;
};

#else

class CIconProviderImpl
{
public:
	// QFileIconProvider draws no distinction between an object's own icon and its type's, so this serves both.
	// The returned icon keeps its several resolutions, which is why this layer hands back a QIcon rather than
	// the single flattened image the Windows shell produces.
	[[nodiscard]] QIcon iconFor(const CFileSystemObject& object) noexcept;

	void setShowOverlayIcons(bool show) noexcept;

private:
	QFileIconProvider _provider;
};

#endif
