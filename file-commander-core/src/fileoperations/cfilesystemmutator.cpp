#include "cfilesystemmutator.h"
#include "operationtesthooks.h"
#include "thiniobridge.h"
#include "filesystemhelperfunctions.h" // caseSensitiveFilesystem

#include "assert/advanced_assert.h"

#ifdef _WIN32
#include "windows/windowsutils.h"

#include <Windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <vector>

namespace
{

// On Windows, only name-surrogate reparse points (symlinks, junctions) are links; other reparse entries
// (OneDrive placeholders and the like) are ordinary files/directories.
bool isLinkEntry(const thin_io::entry_attributes& attributes) noexcept
{
#ifdef _WIN32
	return attributes.is_link && IsReparseTagNameSurrogate(attributes.reparse_tag);
#else
	return attributes.is_link;
#endif
}

CFileSystemError makeError(const FileErrorCategory category, const NativeErrorCode code)
{
	return { category, code, QString::fromStdString(thin_io::format_filesystem_error({ code })) };
}

CFileSystemError unsupportedEntryError(const char* what)
{
	return { FileErrorCategory::Unsupported, 0, QLatin1String(what) };
}

// Rename knows more than the context-free classifier: these codes mean "the destination exists in a form
// the requested rename cannot replace", which must re-enter destination resolution.
CFileSystemError renameErrorFromNative(const NativeErrorCode code)
{
#ifndef _WIN32
	if (code == EISDIR || code == ENOTEMPTY)
		return makeError(FileErrorCategory::AlreadyExists, code);
#endif
	return makeFileSystemError(code);
}

#ifndef _WIN32
// The exclusive rename mechanism itself reporting it is unsupported, as opposed to a real failure.
// All arguments are validated by construction, so EINVAL here means the filesystem cannot service the flag.
bool isExclusiveRenameUnsupported(const NativeErrorCode code) noexcept
{
	return code == EINVAL || code == ENOSYS || code == ENOTSUP
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
		|| code == EOPNOTSUPP
#endif
		;
}
#endif

} // namespace

FileErrorCategory classifyNativeError(const NativeErrorCode code) noexcept
{
#ifdef _WIN32
	switch (code)
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_DIRECTORY: // A path component is not a directory - the ENOTDIR analog
		return FileErrorCategory::NotFound;
	case ERROR_FILE_EXISTS:
	case ERROR_ALREADY_EXISTS:
		return FileErrorCategory::AlreadyExists;
	case ERROR_NOT_SAME_DEVICE:
		return FileErrorCategory::CrossDevice;
	case ERROR_WRITE_PROTECT:
	case ERROR_FILE_READ_ONLY:
		return FileErrorCategory::ReadOnly;
	case ERROR_ACCESS_DENIED:
		return FileErrorCategory::PermissionDenied;
	case ERROR_DISK_FULL:
	case ERROR_HANDLE_DISK_FULL:
	case ERROR_DISK_QUOTA_EXCEEDED:
		return FileErrorCategory::NotEnoughSpace;
	case ERROR_NOT_SUPPORTED:
	case ERROR_INVALID_FUNCTION:
	case ERROR_CALL_NOT_IMPLEMENTED:
		return FileErrorCategory::Unsupported;
	default:
		return FileErrorCategory::IoFailure;
	}
#else
	switch (code)
	{
	case ENOENT:
	case ENOTDIR:
		return FileErrorCategory::NotFound;
	case EEXIST:
		return FileErrorCategory::AlreadyExists;
	case EXDEV:
		return FileErrorCategory::CrossDevice;
	case EROFS:
		return FileErrorCategory::ReadOnly;
	case EACCES:
	case EPERM:
		return FileErrorCategory::PermissionDenied;
	case ENOSPC:
	case EDQUOT:
		return FileErrorCategory::NotEnoughSpace;
	case ENOSYS:
	case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
	case EOPNOTSUPP:
#endif
		return FileErrorCategory::Unsupported;
	default:
		return FileErrorCategory::IoFailure;
	}
#endif
}

CFileSystemError makeFileSystemError(const NativeErrorCode code)
{
	return makeError(classifyNativeError(code), code);
}

std::expected<std::optional<EntrySnapshot>, CFileSystemError> inspectEntry(const CEntryPath& path)
{
	const auto native = thinIoPath(path);
	const auto metadata = thin_io::get_entry_metadata(nativeCStr(native), thin_io::link_behavior::do_not_follow);
	if (!metadata)
	{
		const auto code = metadata.error().native_code;
		if (classifyNativeError(code) == FileErrorCategory::NotFound)
			return std::optional<EntrySnapshot>{};
		return std::unexpected(makeFileSystemError(code));
	}

	if (isLinkEntry(metadata->attributes))
	{
		const auto target = thin_io::get_entry_metadata(nativeCStr(native), thin_io::link_behavior::follow);
		if (!target)
		{
			// Broken or uninspectable target: still an existing link entry. The entry's own kind decides the
			// link kind - a broken junction is a directory entry and must be removable as one.
			const bool directoryEntry = metadata->attributes.kind == thin_io::entry_kind::directory;
			return EntrySnapshot{ path, directoryEntry ? OperationEntryKind::DirectoryLink : OperationEntryKind::FileLink, 0 };
		}

		switch (target->attributes.kind)
		{
		case thin_io::entry_kind::directory:
			return EntrySnapshot{ path, OperationEntryKind::DirectoryLink, 0 };
		case thin_io::entry_kind::regular_file:
			return EntrySnapshot{ path, OperationEntryKind::FileLink, target->logical_size };
		default:
			return EntrySnapshot{ path, OperationEntryKind::Other, 0 };
		}
	}

	switch (metadata->attributes.kind)
	{
	case thin_io::entry_kind::regular_file:
		return EntrySnapshot{ path, OperationEntryKind::RegularFile, metadata->logical_size };
	case thin_io::entry_kind::directory:
		return EntrySnapshot{ path, OperationEntryKind::Directory, 0 };
	default:
		return EntrySnapshot{ path, OperationEntryKind::Other, 0 };
	}
}

std::expected<std::optional<thin_io::entry_identity>, CFileSystemError> readEntryIdentity(const CEntryPath& path, const thin_io::link_behavior linkBehavior)
{
	const auto native = thinIoPath(path);
	const auto metadata = thin_io::get_entry_metadata(nativeCStr(native), linkBehavior);
	if (!metadata)
		return std::unexpected(makeFileSystemError(metadata.error().native_code));

	return metadata->identity;
}

std::expected<SameEntryVerdict, CFileSystemError> checkSameEntry(const CEntryPath& a, const CEntryPath& b, const thin_io::link_behavior linkBehavior)
{
	const auto identityA = readEntryIdentity(a, linkBehavior);
	if (!identityA)
	{
		if (identityA.error().category == FileErrorCategory::NotFound)
			return SameEntryVerdict::Different;
		return std::unexpected(identityA.error());
	}

	const auto identityB = readEntryIdentity(b, linkBehavior);
	if (!identityB)
	{
		if (identityB.error().category == FileErrorCategory::NotFound)
			return SameEntryVerdict::Different;
		return std::unexpected(identityB.error());
	}

	if (!*identityA || !*identityB)
		return SameEntryVerdict::Unknown;

	return **identityA == **identityB ? SameEntryVerdict::Same : SameEntryVerdict::Different;
}

std::expected<bool, CFileSystemError> isEntryWritableNoFollow(const EntrySnapshot& entry)
{
	assert_debug_only(entry.kind == OperationEntryKind::RegularFile);

#ifdef _WIN32
	WCHAR nativePath[32768];
	toUncWcharArray(entry.path.value(), nativePath);

	const DWORD attributes = ::GetFileAttributesW(nativePath); // Reports the entry itself, links are not followed
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
		return std::unexpected(unsupportedEntryError("Writability query is only valid for a non-link regular file"));

	return (attributes & FILE_ATTRIBUTE_READONLY) == 0;
#else
	const auto native = thinIoPath(entry.path);
	struct stat entryStat;
	if (::lstat(nativeCStr(native), &entryStat) != 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if (!S_ISREG(entryStat.st_mode))
		return std::unexpected(unsupportedEntryError("Writability query is only valid for a non-link regular file"));

	if (::geteuid() == 0)
		return true;
	if (entryStat.st_uid == ::geteuid())
		return (entryStat.st_mode & S_IWUSR) != 0;
	if (entryStat.st_gid == ::getegid()) // Supplementary groups are not consulted; close enough for a remediation prompt
		return (entryStat.st_mode & S_IWGRP) != 0;
	return (entryStat.st_mode & S_IWOTH) != 0;
#endif
}

std::expected<CopyableDirectoryTimes, CFileSystemError> readCopyableDirectoryTimes(const CEntryPath& source)
{
	const auto native = thinIoPath(source);
	const auto times = thin_io::get_times(nativeCStr(native));
	if (!times)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if (!times->last_write) [[unlikely]]
		return std::unexpected(CFileSystemError{ FileErrorCategory::IoFailure, 0, QStringLiteral("The filesystem does not report a last-write time") });

	CopyableDirectoryTimes result{ .creation = {}, .lastWrite = *times->last_write };
	if constexpr (thin_io::creation_time_settable)
		result.creation = times->creation;
	return result;
}

std::expected<void, CFileSystemError> CFileSystemMutator::renameEntry(const CEntryPath& source, const CEntryPath& destination, const ReplacementMode replacement)
{
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

#ifdef _WIN32
	WCHAR sourceNative[32768], destinationNative[32768];
	toUncWcharArray(source.value(), sourceNative);
	toUncWcharArray(destination.value(), destinationNative);

	// Flag 0 is the native exclusive mechanism: the API refuses an existing destination on every filesystem,
	// so no unsupported-degradation path exists on Windows. It also permits a case-only rename of the same entry.
	const DWORD flags = replacement == ReplacementMode::ReplaceExistingFile ? MOVEFILE_REPLACE_EXISTING : 0;

	if (const auto forcedError = fireHook(Point::RenameEntry_Native))
		return std::unexpected(renameErrorFromNative(*forcedError));

	if (::MoveFileExW(sourceNative, destinationNative, flags) != 0)
		return {};

	return std::unexpected(renameErrorFromNative(captureNativeError()));
#else
	const auto sourceNative = thinIoPath(source);
	const auto destinationNative = thinIoPath(destination);

	if (replacement == ReplacementMode::ReplaceExistingFile)
	{
		// POSIX rename() silently replaces an empty destination directory when the source is one, and
		// directory replacement is never authorized. Windows enforces this natively; here it must be rejected.
		struct stat destinationStat;
		if (::lstat(nativeCStr(destinationNative), &destinationStat) == 0 && S_ISDIR(destinationStat.st_mode))
			return std::unexpected(makeError(FileErrorCategory::AlreadyExists, EISDIR));

		if (const auto forcedError = fireHook(Point::RenameEntry_Native))
			return std::unexpected(renameErrorFromNative(*forcedError));

		if (::rename(nativeCStr(sourceNative), nativeCStr(destinationNative)) == 0)
			return {};

		return std::unexpected(renameErrorFromNative(captureNativeError()));
	}

	// RequireAbsent: the native exclusive mechanism, never check-then-rename.
	NativeErrorCode errorCode;
	if (const auto forcedError = fireHook(Point::RenameEntry_Native))
		errorCode = *forcedError;
	else
	{
#if defined __linux__
		if (::renameat2(AT_FDCWD, nativeCStr(sourceNative), AT_FDCWD, nativeCStr(destinationNative), RENAME_NOREPLACE) == 0)
			return {};
		errorCode = captureNativeError();
#elif defined __APPLE__
		if (::renamex_np(nativeCStr(sourceNative), nativeCStr(destinationNative), RENAME_EXCL) == 0)
			return {};
		errorCode = captureNativeError();
#else
		errorCode = ENOSYS; // No exclusive-rename mechanism on this platform - degrade below
#endif
	}

	if (isExclusiveRenameUnsupported(errorCode))
	{
		// The failed exclusive call is the probe (never cached): fresh no-follow recheck, then plain rename.
		// This shrinks the unprotected window to the recheck-rename instant, on such filesystems only.
		const auto destinationMetadata = thin_io::get_entry_metadata(nativeCStr(destinationNative), thin_io::link_behavior::do_not_follow);
		if (destinationMetadata)
			return std::unexpected(makeError(FileErrorCategory::AlreadyExists, EEXIST));
		if (classifyNativeError(destinationMetadata.error().native_code) != FileErrorCategory::NotFound)
			return std::unexpected(makeFileSystemError(destinationMetadata.error().native_code));

		if (::rename(nativeCStr(sourceNative), nativeCStr(destinationNative)) == 0)
			return {};

		return std::unexpected(renameErrorFromNative(captureNativeError()));
	}

	if constexpr (!caseSensitiveFilesystem())
	{
		// A case-only rename addresses the same entry, so the exclusive mechanism sees the destination as
		// occupied - by the source itself. Plain rename changes the case in place; at worst it replaces the
		// entry with itself, so this is not a check-then-replace hole.
		if (classifyNativeError(errorCode) == FileErrorCategory::AlreadyExists
			&& source.value().compare(destination.value(), Qt::CaseInsensitive) == 0
			&& source.value() != destination.value())
		{
			if (::rename(nativeCStr(sourceNative), nativeCStr(destinationNative)) == 0)
				return {};
			return std::unexpected(renameErrorFromNative(captureNativeError()));
		}
	}

	return std::unexpected(renameErrorFromNative(errorCode));
#endif
}

std::expected<void, CFileSystemError> CFileSystemMutator::removeEntry(const EntrySnapshot& entry)
{
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

	if (const auto forcedError = fireHook(Point::RemoveEntry_Native))
		return std::unexpected(makeFileSystemError(*forcedError));

#ifdef _WIN32
	WCHAR nativePath[32768];
	toUncWcharArray(entry.path.value(), nativePath);

	// Directory entries - real or links (junctions, directory symlinks) - are removed with RemoveDirectory,
	// which deletes the entry without following it; everything else, including file symlinks, with DeleteFile.
	const bool isDirectoryEntry = entry.kind == OperationEntryKind::Directory || entry.kind == OperationEntryKind::DirectoryLink;
	if ((isDirectoryEntry ? ::RemoveDirectoryW(nativePath) : ::DeleteFileW(nativePath)) != 0)
		return {};

	return std::unexpected(makeFileSystemError(captureNativeError()));
#else
	const auto native = thinIoPath(entry.path);

	// Only a real directory takes rmdir; a directory symlink is itself a link entry and must be unlink()ed -
	// rmdir would refuse it, and nothing here may ever address the target.
	if ((entry.kind == OperationEntryKind::Directory ? ::rmdir(nativeCStr(native)) : ::unlink(nativeCStr(native))) == 0)
		return {};

	return std::unexpected(makeFileSystemError(captureNativeError()));
#endif
}

namespace
{

// One native mkdir. On failure writes the immediately captured (or forced) error code.
bool createOneDirectory(const CEntryPath& path, NativeErrorCode& errorCode, const bool isFinalComponent)
{
	if (isFinalComponent)
	{
		if (const auto forcedError = OperationTestHooks::fireHook(OperationTestHooks::Point::CreateDirectory_FinalNative))
		{
			errorCode = *forcedError;
			return false;
		}
	}

#ifdef _WIN32
	WCHAR nativePath[32768];
	toUncWcharArray(path.value(), nativePath);
	if (::CreateDirectoryW(nativePath, nullptr) != 0)
		return true;
#else
	const auto native = thinIoPath(path);
	if (::mkdir(nativeCStr(native), 0777) == 0)
		return true;
#endif

	errorCode = captureNativeError();
	return false;
}

} // namespace

std::expected<DirectoryCreationOutcome, CFileSystemError> CFileSystemMutator::createDirectories(const CEntryPath& path)
{
	NativeErrorCode errorCode;
	if (createOneDirectory(path, errorCode, true))
		return DirectoryCreationOutcome::CreatedFinalDirectory;

	if (classifyNativeError(errorCode) == FileErrorCategory::NotFound)
	{
		// Parents missing: create the chain top-down, then retry the final component once.
		std::vector<CEntryPath> missingChain;
		for (CEntryPath ancestor = path.parent(); !ancestor.isRoot(); ancestor = ancestor.parent())
			missingChain.push_back(ancestor);

		for (auto it = missingChain.rbegin(); it != missingChain.rend(); ++it)
		{
			NativeErrorCode parentErrorCode;
			if (!createOneDirectory(*it, parentErrorCode, false) && classifyNativeError(parentErrorCode) != FileErrorCategory::AlreadyExists)
				return std::unexpected(makeFileSystemError(parentErrorCode));
		}

		if (createOneDirectory(path, errorCode, true))
			return DirectoryCreationOutcome::CreatedFinalDirectory;
	}

	if (classifyNativeError(errorCode) == FileErrorCategory::AlreadyExists)
	{
		// mkdir reports the collision for any entry type; only a directory (or directory link) counts as
		// the final directory already existing. Anything else re-enters resolution as a collision error.
		const auto existing = inspectEntry(path);
		if (!existing)
			return std::unexpected(existing.error());
		if (*existing && ((*existing)->kind == OperationEntryKind::Directory || (*existing)->kind == OperationEntryKind::DirectoryLink))
			return DirectoryCreationOutcome::FinalDirectoryAlreadyExisted;
	}

	return std::unexpected(makeFileSystemError(errorCode));
}

std::expected<void, CFileSystemError> CFileSystemMutator::applyDirectoryTimes(const CEntryPath& destination, const CopyableDirectoryTimes& times)
{
	const thin_io::entry_times nativeTimes{ .creation = times.creation, .last_access = {}, .last_write = times.lastWrite };

	const auto native = thinIoPath(destination);
	if (!thin_io::set_times(nativeCStr(native), nativeTimes))
		return std::unexpected(makeFileSystemError(captureNativeError()));

	return {};
}

std::expected<void, CFileSystemError> CFileSystemMutator::setEntryWritable(const EntrySnapshot& entry, const bool writable)
{
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

	assert_debug_only(entry.kind == OperationEntryKind::RegularFile);

#ifdef _WIN32
	WCHAR nativePath[32768];
	toUncWcharArray(entry.path.value(), nativePath);

	const DWORD attributes = ::GetFileAttributesW(nativePath);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
		return std::unexpected(unsupportedEntryError("Writability change is only valid for a non-link regular file"));

	const DWORD newAttributes = writable ? attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY) : attributes | FILE_ATTRIBUTE_READONLY;
	if (newAttributes == attributes)
		return {};

	if (const auto forcedError = fireHook(Point::SetEntryWritable_Native))
		return std::unexpected(makeFileSystemError(*forcedError));

	if (::SetFileAttributesW(nativePath, newAttributes) == 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	return {};
#else
	const auto native = thinIoPath(entry.path);

	// lstat first: chmod() follows links, and a link (or anything but a regular file) must never be remediated.
	// The remaining lstat-chmod window is accepted; callers re-inspect freshly around every remediation decision.
	struct stat entryStat;
	if (::lstat(nativeCStr(native), &entryStat) != 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if (!S_ISREG(entryStat.st_mode))
		return std::unexpected(unsupportedEntryError("Writability change is only valid for a non-link regular file"));

	const mode_t newMode = writable ? entryStat.st_mode | S_IWUSR : entryStat.st_mode & ~static_cast<mode_t>(S_IWUSR | S_IWGRP | S_IWOTH);
	if (newMode == entryStat.st_mode)
		return {};

	if (const auto forcedError = fireHook(Point::SetEntryWritable_Native))
		return std::unexpected(makeFileSystemError(*forcedError));

	if (::chmod(nativeCStr(native), newMode) != 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	return {};
#endif
}
