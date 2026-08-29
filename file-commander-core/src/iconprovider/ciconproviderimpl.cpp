#include "ciconproviderimpl.h"
#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QImage>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32

#include <Windows.h>
#include <shellapi.h>

static UINT iconRetrievalFlags(const bool showOverlayIcons) noexcept
{
	return SHGFI_ICON | SHGFI_SMALLICON | (showOverlayIcons ? SHGFI_ADDOVERLAYS : 0);
}

// Converts the icon SHGetFileInfoW produced and releases it. Empty if the call failed or yielded no icon.
static QImage imageFromShellFileInfo(const DWORD_PTR shGetFileInfoResult, const SHFILEINFOW& info) noexcept
{
	if (shGetFileInfoResult == 0 || info.hIcon == nullptr)
		return {};

	QImage image = QImage::fromHICON(info.hIcon);
	DestroyIcon(info.hIcon);
	return image;
}

QImage CIconProviderImpl::genericIconImage(const QString& extension, const bool isDir) const noexcept
{
	// SHGFI_USEFILEATTRIBUTES makes the shell answer from the name and the attributes alone, without a disk access,
	// so the name below need not exist.
	const UINT flags = iconRetrievalFlags(_showOverlayIcons) | SHGFI_USEFILEATTRIBUTES;

	SHFILEINFOW info;
	if (isDir)
	{
		const auto result = SHGetFileInfoW(L"a", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info), flags);
		return imageFromShellFileInfo(result, info);
	}

	// An empty extension yields a special "system" icon, so it is replaced rather than queried as a bare dot.
	const QString name = '.' % (extension.isEmpty() ? QStringLiteral("1") : extension);
	const auto result = SHGetFileInfoW(reinterpret_cast<const wchar_t*>(name.utf16()), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags);
	return imageFromShellFileInfo(result, info);
}

QImage CIconProviderImpl::preciseIconImage(const QString& fullAbsolutePath) const noexcept
{
	// Without SHGFI_USEFILEATTRIBUTES the shell reads the object itself and dwFileAttributes is ignored.
	const UINT flags = iconRetrievalFlags(_showOverlayIcons);
	const QString path = toNativeSeparators(fullAbsolutePath); // SHGetFileInfoW does not accept UNC paths!

	SHFILEINFOW info;
	const auto result = SHGetFileInfoW(reinterpret_cast<const wchar_t*>(path.utf16()), 0, &info, sizeof(info), flags);
	return imageFromShellFileInfo(result, info);
}

void CIconProviderImpl::setShowOverlayIcons(const bool show) noexcept
{
	_showOverlayIcons = show;
}

#else // ! _WIN32

QIcon CIconProviderImpl::iconFor(const CFileSystemObject& object) noexcept
{
#ifndef CFILESYSTEMOBJECT_TEST // TODO: Remove this ugly hack
	return _provider.icon(object.qFileInfo());
#else
	return {};
#endif
}

void CIconProviderImpl::setShowOverlayIcons(const bool show) noexcept
{
	const auto oldOptions = _provider.options();
	auto newOptions = oldOptions;
	if (show)
		newOptions |= QFileIconProvider::DontUseCustomDirectoryIcons;
	else
		newOptions &= (~QFileIconProvider::DontUseCustomDirectoryIcons);

	if (oldOptions != newOptions)
		_provider.setOptions(newOptions);
}

#endif
