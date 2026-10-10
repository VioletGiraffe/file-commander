#include "cfilesystemmutator.h"

#include "operationtesthooks.h"
#include "thiniobridge.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QUuid>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include "windows_path_win.hpp" // thin_io

#include <Windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <utility>
#include <vector>

namespace
{

CFileSystemError makeError(const FileErrorCategory category, const NativeErrorCode code)
{
	return { category, code, QString::fromStdString(thin_io::format_filesystem_error({ code })) };
}

CFileSystemError unsupportedEntryError(const char* what)
{
	return { FileErrorCategory::Unsupported, 0, QLatin1String(what) };
}

std::expected<thin_io::entry_metadata, CFileSystemError> readIdentityMetadata(
	const CEntryPath& path, const thin_io::link_behavior linkBehavior)
{
	const auto native = thinIoPath(path);
	auto metadata = thin_io::get_entry_metadata(nativeCStr(native), linkBehavior);
	if (!metadata)
		return std::unexpected(makeFileSystemError(metadata.error().native_code));
	return std::move(*metadata);
}

#ifdef _WIN32

// A path prepared for one of this module's own Win32 calls exactly as thin_io prepares the ones it makes itself,
// so the same entry is addressed either way: the extended-length prefix in both its drive and \\?\UNC\ forms, and
// a reported error where a fixed-size buffer used to truncate. Holds the buffer inline - construct it in place.
class Win32Path
{
public:
	explicit Win32Path(const CEntryPath& path) : _buffer{ reinterpret_cast<const wchar_t*>(nativePathValue(path).utf16()) } {}

	[[nodiscard]] inline explicit operator bool() const noexcept { return static_cast<bool>(_buffer); }
	[[nodiscard]] inline const wchar_t* c_str() const noexcept { return _buffer.c_str(); }
	[[nodiscard]] inline NativeErrorCode error() const noexcept { return static_cast<NativeErrorCode>(_buffer.error_code()); }

private:
	thin_io::windows_path_buffer _buffer;
};

std::optional<NativeErrorCode> moveFileError(const wchar_t* source, const wchar_t* destination, const DWORD flags)
{
	if (const auto forcedError = OperationTestHooks::fireHook(OperationTestHooks::Point::RenameEntry_Native))
		return *forcedError;

	if (::MoveFileExW(source, destination, flags) != 0)
		return {};
	return captureNativeError();
}

#endif

std::optional<NativeErrorCode> setPermissionsNoFollowError(const NativePathString& path, const thin_io::file_permissions permissions)
{
	if (const auto forcedError = OperationTestHooks::fireHook(OperationTestHooks::Point::SetEntryWritable_Native))
		return *forcedError;

	if (const auto result = thin_io::set_permissions(nativeCStr(path), permissions, thin_io::link_behavior::do_not_follow); !result)
		return result.error().native_code;
	return {};
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
std::expected<bool, CFileSystemError> processHasSupplementaryGroup(const gid_t group)
{
	const int groupCount = ::getgroups(0, nullptr);
	if (groupCount < 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));
	if (groupCount == 0)
		return false;

	std::vector<gid_t> groups(static_cast<size_t>(groupCount));
	const int returnedCount = ::getgroups(groupCount, groups.data());
	if (returnedCount < 0)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	groups.resize(static_cast<size_t>(returnedCount));
	for (const gid_t candidate : groups)
	{
		if (candidate == group)
			return true;
	}
	return false;
}

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

std::optional<NativeErrorCode> exclusiveRenameError(const NativePathString& source, const NativePathString& destination,
	const OperationTestHooks::Point hookPoint)
{
	if (const auto forcedError = OperationTestHooks::fireHook(hookPoint))
		return *forcedError;

#if defined __linux__
	if (::renameat2(AT_FDCWD, nativeCStr(source), AT_FDCWD, nativeCStr(destination), RENAME_NOREPLACE) == 0)
		return {};
#elif defined __APPLE__
	if (::renamex_np(nativeCStr(source), nativeCStr(destination), RENAME_EXCL) == 0)
		return {};
#else
	return ENOSYS;
#endif
	return captureNativeError();
}

CFileSystemError exclusiveRenameFailure(const NativeErrorCode code)
{
	if (isExclusiveRenameUnsupported(code))
		return makeError(FileErrorCategory::Unsupported, code);
	return renameErrorFromNative(code);
}

CFileSystemError caseRespellRollbackFailure(const CEntryPath& temporaryPath, const NativeErrorCode publicationCode,
	const NativeErrorCode rollbackCode)
{
	const CFileSystemError publicationError = exclusiveRenameFailure(publicationCode);
	const CFileSystemError rollbackError = exclusiveRenameFailure(rollbackCode);
	return { FileErrorCategory::IoFailure, rollbackCode,
		QStringLiteral("The requested case respelling failed (%1), and the source could not be restored (%2). The source remains at: %3")
			.arg(publicationError.diagnostic, rollbackError.diagnostic, temporaryPath.value()) };
}

std::expected<void, CFileSystemError> renameCaseOnlyThroughTemporary(const CEntryPath& source, const NativePathString& sourceNative,
	const NativePathString& destinationNative)
{
	static constexpr int maximumTemporaryNameAttempts = 10;
	std::optional<CEntryPath> temporaryPath;
	NativePathString temporaryNative;
	for (int attempt = 0; attempt < maximumTemporaryNameAttempts; ++attempt)
	{
		CEntryPath candidate = source.parent().child(
			QStringLiteral(".file-commander-rename-") % QUuid::createUuid().toString(QUuid::WithoutBraces) % QStringLiteral(".tmp"));
		auto candidateNative = thinIoPath(candidate);
		const auto moveError = exclusiveRenameError(sourceNative, candidateNative, OperationTestHooks::Point::CaseRespell_MoveToTemporary_Native);
		if (!moveError)
		{
			temporaryPath = std::move(candidate);
			temporaryNative = std::move(candidateNative);
			break;
		}
		CFileSystemError moveFailure = exclusiveRenameFailure(*moveError);
		if (moveFailure.category != FileErrorCategory::AlreadyExists)
			return std::unexpected(std::move(moveFailure));
	}

	if (!temporaryPath)
	{
		return std::unexpected(CFileSystemError{ FileErrorCategory::IoFailure, EEXIST,
			QStringLiteral("Could not reserve a unique temporary name for the case respelling") });
	}

	const auto publicationError = exclusiveRenameError(temporaryNative, destinationNative,
		OperationTestHooks::Point::CaseRespell_PublishTemporary_Native);
	if (!publicationError)
		return {};

	const auto rollbackError = exclusiveRenameError(temporaryNative, sourceNative,
		OperationTestHooks::Point::CaseRespell_RestoreSource_Native);
	if (rollbackError)
		return std::unexpected(caseRespellRollbackFailure(*temporaryPath, *publicationError, *rollbackError));

	return std::unexpected(exclusiveRenameFailure(*publicationError));
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
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

	if (const auto forcedError = fireHook(Point::InspectEntry_Native))
	{
		if (classifyNativeError(*forcedError) == FileErrorCategory::NotFound)
			return std::optional<EntrySnapshot>{};
		return std::unexpected(makeFileSystemError(*forcedError));
	}

	const auto native = thinIoPath(path);
	const auto metadata = thin_io::get_entry_metadata(nativeCStr(native), thin_io::link_behavior::do_not_follow);
	if (!metadata)
	{
		const auto code = metadata.error().native_code;
		if (classifyNativeError(code) == FileErrorCategory::NotFound)
			return std::optional<EntrySnapshot>{};
		return std::unexpected(makeFileSystemError(code));
	}

	if (metadata->attributes.is_link)
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
	auto metadata = readIdentityMetadata(path, linkBehavior);
	if (!metadata)
		return std::unexpected(std::move(metadata.error()));

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

std::expected<std::optional<DirectoryTraversalIdentity>, CFileSystemError> readDirectoryTraversalIdentity(
	const CEntryPath& path, const thin_io::link_behavior linkBehavior)
{
	auto metadata = readIdentityMetadata(path, linkBehavior);
	if (!metadata)
		return std::unexpected(std::move(metadata.error()));
	if (!metadata->identity)
		return std::optional<DirectoryTraversalIdentity>{};

	// thin_io reports a distinct mount ID where the OS exposes one (notably STATX_MNT_ID on Linux). Falling back
	// to the object's filesystem preserves the former object-only cycle detection on older kernels, other POSIX
	// systems, and Windows, without making mount-view identity a prerequisite for safe bounded traversal.
	return DirectoryTraversalIdentity{
		.entry = *metadata->identity,
		.mount = metadata->mount_id.value_or(metadata->identity->filesystem)
	};
}

std::expected<bool, CFileSystemError> isEntryWritableNoFollow(const EntrySnapshot& entry)
{
	assert_debug_only(entry.kind == OperationEntryKind::RegularFile);

#ifdef _WIN32
	const auto native = thinIoPath(entry.path);
	const auto status = thin_io::get_entry_status(nativeCStr(native), thin_io::link_behavior::do_not_follow);
	if (!status)
		return std::unexpected(makeFileSystemError(status.error().native_code));

	if (status->attributes.is_link || status->attributes.kind != thin_io::entry_kind::regular_file)
		return std::unexpected(unsupportedEntryError("Writability query is only valid for a non-link regular file"));

	return !status->permissions->read_only;
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

	const bool groupWritable = (entryStat.st_mode & S_IWGRP) != 0;
	const bool otherWritable = (entryStat.st_mode & S_IWOTH) != 0;
	if (entryStat.st_gid == ::getegid())
		return groupWritable;
	if (groupWritable == otherWritable) // Membership cannot change the answer
		return groupWritable;

	// POSIX permits getgroups() to include or omit the effective GID; it was checked separately above.
	const auto supplementaryGroup = processHasSupplementaryGroup(entryStat.st_gid);
	if (!supplementaryGroup)
		return std::unexpected(supplementaryGroup.error());
	return *supplementaryGroup ? groupWritable : otherWritable;
#endif
}

std::expected<CopyableDirectoryTimes, CFileSystemError> readCopyableDirectoryTimes(const CEntryPath& source)
{
	const auto native = thinIoPath(source);
	const auto times = thin_io::get_times(nativeCStr(native));
	if (!times)
		return std::unexpected(makeFileSystemError(captureNativeError()));

	if (!times->last_write.is_set()) [[unlikely]]
		return std::unexpected(CFileSystemError{ FileErrorCategory::IoFailure, 0, QStringLiteral("The filesystem does not report a last-write time") });

	CopyableDirectoryTimes result{ .creation = {}, .lastWrite = times->last_write };
	if constexpr (thin_io::creation_time_settable)
		result.creation = times->creation;
	return result;
}

std::expected<void, CFileSystemError> CFileSystemMutator::renameEntry(const CEntryPath& source, const CEntryPath& destination, const ReplacementMode replacement)
{
#ifdef _WIN32
	const Win32Path sourceNative{ source }, destinationNative{ destination };
	if (!sourceNative) [[unlikely]]
		return std::unexpected(makeFileSystemError(sourceNative.error()));
	if (!destinationNative) [[unlikely]]
		return std::unexpected(makeFileSystemError(destinationNative.error()));

	// Flag 0 is the native exclusive mechanism; no unsupported-degradation path exists on Windows. It has a
	// same-file exemption: a destination that is another name for the source file does not count as occupied.
	// That is what permits case-only renames - and it also lets a rename onto a hardlink alias succeed by
	// removing the source name (accepted divergence, see the design plan's same-object note: POSIX exclusive
	// rename refuses same-inode destinations).
	const DWORD flags = replacement == ReplacementMode::ReplaceExistingFile ? MOVEFILE_REPLACE_EXISTING : 0;

	auto moveError = moveFileError(sourceNative.c_str(), destinationNative.c_str(), flags);
	if (!moveError)
		return {};

	// ERROR_ACCESS_DENIED is also how MoveFileExW reports an unreplaceable directory-bearing destination.
	// Refine only from fresh no-follow attributes; ordinary permission failures remain permission failures.
	if (replacement == ReplacementMode::ReplaceExistingFile && *moveError == ERROR_ACCESS_DENIED)
	{
		const auto destinationForThinIo = thinIoPath(destination);
		const auto destinationStatus = thin_io::get_entry_status(nativeCStr(destinationForThinIo), thin_io::link_behavior::do_not_follow);
		if (destinationStatus && destinationStatus->attributes.kind == thin_io::entry_kind::directory)
			return std::unexpected(makeError(FileErrorCategory::AlreadyExists, *moveError));

		const bool readOnlyRegularFile = destinationStatus && !destinationStatus->attributes.is_link && destinationStatus->permissions->read_only;
		if (readOnlyRegularFile)
		{
			const thin_io::file_permissions originalPermissions = *destinationStatus->permissions;
			thin_io::file_permissions writablePermissions = originalPermissions;
			writablePermissions.read_only = false;
			if (const auto attributeError = setPermissionsNoFollowError(destinationForThinIo, writablePermissions))
				return std::unexpected(makeFileSystemError(*attributeError));

			moveError = moveFileError(sourceNative.c_str(), destinationNative.c_str(), flags);
			if (!moveError)
				return {};

			if (const auto restorationError = setPermissionsNoFollowError(destinationForThinIo, originalPermissions))
			{
				const CFileSystemError replacementFailure = renameErrorFromNative(*moveError);
				const CFileSystemError restorationFailure = makeFileSystemError(*restorationError);
				return std::unexpected(CFileSystemError{ FileErrorCategory::IoFailure, *restorationError,
					QStringLiteral("Replacement failed (%1), and the destination's read-only attribute could not be restored (%2)")
						.arg(replacementFailure.diagnostic, restorationFailure.diagnostic) });
			}

			// The retry opens a new race window: preserve the directory-collision refinement if one appeared
			// after the read-only file was inspected.
			if (*moveError == ERROR_ACCESS_DENIED)
			{
				const auto freshStatus = thin_io::get_entry_status(nativeCStr(destinationForThinIo), thin_io::link_behavior::do_not_follow);
				if (freshStatus && freshStatus->attributes.kind == thin_io::entry_kind::directory)
					return std::unexpected(makeError(FileErrorCategory::AlreadyExists, *moveError));
			}
		}
	}

	return std::unexpected(renameErrorFromNative(*moveError));
#else
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

	const auto sourceNative = thinIoPath(source);
	const auto destinationNative = thinIoPath(destination);

	if (replacement == ReplacementMode::ReplaceExistingFile)
	{
		// POSIX rename() silently replaces an empty destination directory when the source is one, and
		// directory replacement is never authorized. Windows enforces this natively; here it must be rejected.
		const auto destinationStatus = thin_io::get_entry_status(nativeCStr(destinationNative), thin_io::link_behavior::do_not_follow);
		if (destinationStatus && destinationStatus->attributes.kind == thin_io::entry_kind::directory)
			return std::unexpected(makeError(FileErrorCategory::AlreadyExists, EISDIR));

		if (const auto forcedError = fireHook(Point::RenameEntry_Native))
			return std::unexpected(renameErrorFromNative(*forcedError));

		if (::rename(nativeCStr(sourceNative), nativeCStr(destinationNative)) == 0)
			return {};

		return std::unexpected(renameErrorFromNative(captureNativeError()));
	}

	// RequireAbsent starts with the native exclusive mechanism.
	const auto exclusiveError = exclusiveRenameError(sourceNative, destinationNative, Point::RenameEntry_Native);
	if (!exclusiveError)
		return {};
	const NativeErrorCode errorCode = *exclusiveError;

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

	// An exclusive case-respell can collide with the source entry itself on a case-insensitive filesystem.
	// Moving through a unique sibling distinguishes that from a real case-sensitive collision without guessing
	// volume semantics: the requested spelling becomes free only when both spellings were one directory entry.
	if (classifyNativeError(errorCode) == FileErrorCategory::AlreadyExists
		&& source.value().compare(destination.value(), Qt::CaseInsensitive) == 0
		&& source.value() != destination.value())
	{
		return renameCaseOnlyThroughTemporary(source, sourceNative, destinationNative);
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
	const Win32Path nativePath{ entry.path };
	if (!nativePath) [[unlikely]]
		return std::unexpected(makeFileSystemError(nativePath.error()));

	// Directory entries - real or links (junctions, directory symlinks) - are removed with RemoveDirectory,
	// which deletes the entry without following it; everything else, including file symlinks, with DeleteFile.
	const bool isDirectoryEntry = entry.kind == OperationEntryKind::Directory || entry.kind == OperationEntryKind::DirectoryLink;
	if ((isDirectoryEntry ? ::RemoveDirectoryW(nativePath.c_str()) : ::DeleteFileW(nativePath.c_str())) != 0)
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
	const Win32Path nativePath{ path };
	if (!nativePath) [[unlikely]]
	{
		errorCode = nativePath.error();
		return false;
	}

	if (::CreateDirectoryW(nativePath.c_str(), nullptr) != 0)
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
		// Preserve a proven collision as an outcome; the destination resolver owns its entry-kind policy.
		// A phantom AlreadyExists remains an error rather than becoming an invisible create/resolve loop.
		const auto existing = inspectEntry(path);
		if (!existing)
			return std::unexpected(existing.error());
		if (existing->has_value())
			return DirectoryCreationOutcome::FinalEntryAlreadyExisted;
	}

	return std::unexpected(makeFileSystemError(errorCode));
}

std::expected<void, CFileSystemError> CFileSystemMutator::applyDirectoryTimes(const CEntryPath& destination, const CopyableDirectoryTimes& times)
{
	using OperationTestHooks::fireHook, OperationTestHooks::Point;

	if (const auto forcedError = fireHook(Point::ApplyDirectoryTimes_Native))
		return std::unexpected(makeFileSystemError(*forcedError));

	const thin_io::entry_times nativeTimes{ .creation = times.creation, .last_access = {}, .last_write = times.lastWrite };

	const auto native = thinIoPath(destination);
	if (!thin_io::set_times(nativeCStr(native), nativeTimes))
		return std::unexpected(makeFileSystemError(captureNativeError()));

	return {};
}

std::expected<void, CFileSystemError> CFileSystemMutator::setEntryWritable(const EntrySnapshot& entry, const bool writable)
{
	assert_debug_only(entry.kind == OperationEntryKind::RegularFile);

	const auto native = thinIoPath(entry.path);
	const auto status = thin_io::get_entry_status(nativeCStr(native), thin_io::link_behavior::do_not_follow);
	if (!status)
		return std::unexpected(makeFileSystemError(status.error().native_code));

	if (status->attributes.is_link || status->attributes.kind != thin_io::entry_kind::regular_file)
		return std::unexpected(unsupportedEntryError("Writability change is only valid for a non-link regular file"));

	thin_io::file_permissions permissions = *status->permissions;
#ifdef _WIN32
	permissions.read_only = !writable;
#else
	permissions.mode = writable ? permissions.mode | S_IWUSR : permissions.mode & ~static_cast<uint32_t>(S_IWUSR | S_IWGRP | S_IWOTH);
#endif
	if (permissions == *status->permissions)
		return {};

	if (const auto error = setPermissionsNoFollowError(native, permissions))
		return std::unexpected(makeFileSystemError(*error));

	return {};
}
