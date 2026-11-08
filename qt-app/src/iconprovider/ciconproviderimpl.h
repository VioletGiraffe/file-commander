#pragma once

#ifndef _WIN32
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFileIconProvider>
RESTORE_COMPILER_WARNINGS
#endif

#include <atomic>

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
	[[nodiscard]] QImage preciseIconImage(const QString& fullAbsolutePath) const noexcept;

	// Safe to call while the retrieval thread is running: it reads the flag once per query.
	void setShowOverlayIcons(bool show) noexcept;

private:
	std::atomic<bool> _showOverlayIcons{ false };
};

#else

class CIconProviderImpl
{
public:
	// The object's own icon. Accesses the disk.
	// The returned icon keeps its several resolutions, which is why this layer hands back a QIcon rather than
	// the single flattened image the Windows shell produces.
	[[nodiscard]] QIcon iconFor(const QString& fullAbsolutePath) noexcept;
#ifndef __APPLE__
	// The icon theme's icon for the extension's MIME type, or the folder icon when isDir. Never accesses the disk.
	[[nodiscard]] QIcon genericIcon(const QString& extension, bool isDir) const noexcept;
#endif

	void setShowOverlayIcons(bool show) noexcept;

private:
	QFileIconProvider _provider;
};

#endif
