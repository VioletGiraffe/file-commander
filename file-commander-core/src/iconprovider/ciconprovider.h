#pragma once

#include "detail/hashmap_helpers.h"

#include "compiler/compiler_warnings_control.h"

#include <3rdparty/ankerl/unordered_dense.h>

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <optional>
#include <stdint.h>
#include <time.h>

class CFileSystemObject;
class CIconProviderImpl;

// Answers icon queries for filesystem objects and caches the answers. Owned by CController and confined to the UI
// thread. A query that fails is never cached, so a null return always means "no icon", never "no icon yet".
class CIconProvider
{
public:
	CIconProvider();
	~CIconProvider();

	CIconProvider(const CIconProvider&) = delete;
	CIconProvider& operator=(const CIconProvider&) = delete;

	// The icon of the object's type. Never accesses the disk.
	[[nodiscard]] QIcon genericIconForExtension(const CFileSystemObject& object);
	// The object's own icon, which .exe, .ico and .lnk derive from their contents. Accesses the disk on a cache
	// miss, so only for callers that ask about a handful of objects at a time.
	[[nodiscard]] QIcon preciseIconBlocking(const CFileSystemObject& object);

	// Re-reads the overlay setting and drops every cached icon, which the setting changes.
	void settingsChanged();

private:
	struct CachedIcon {
		uint64_t contentHash;
		time_t modificationTime; // The object's, as of retrieval. A newer one means the icon may have changed with it.
	};

	struct FetchedIcon {
		QIcon icon;
		uint64_t contentHash = 0;
	};

	[[nodiscard]] FetchedIcon fetchPreciseIcon(const CFileSystemObject& object) const;
	// Null unless the object has an entry that its current modification time still matches.
	[[nodiscard]] QIcon cachedPreciseIcon(uint64_t objectHash, const CFileSystemObject& object) const;
	void cachePreciseIcon(uint64_t objectHash, const CFileSystemObject& object, FetchedIcon&& fetched);

private:
	// Keyed on CFileSystemObject::hash(), the hash of the full path, so an entry is only ever reused for the path it
	// was made for. The indirection through the content hash lets a folder of same-type files share one stored icon.
	ankerl::unordered_dense::map<uint64_t, CachedIcon, IdentityHash> _cachedIconByObjectHash;
	ankerl::unordered_dense::segmented_map<uint64_t, QIcon, IdentityHash> _iconByContentHash;

	ankerl::unordered_dense::segmented_map<QString, QIcon, QStringHash> _genericIconByExtension;
	std::optional<QIcon> _genericFolderIcon;

	std::unique_ptr<CIconProviderImpl> _provider;
};
