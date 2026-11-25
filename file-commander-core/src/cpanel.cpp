#include "cpanel.h"

#include "settings.h"
#include "filesystemhelperfunctions.h"
#include "panellisting.h"
#include "filesystemhelpers/filestatistics.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "directoryscan/directoryscanner.h"
#include "directoryscan/thinioqstring.h"
#include "paths/pathsyntax.h"
#include "std_helpers/qt_container_helpers.hpp"
#include "timing/ctimeelapsed.h"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <optional>


enum {
	ContentsChangedNotificationTag
};

// A flattened view's in-place updates start at most this often: the GUI's relayout after inserting or removing rows
// grows with the rows. See core-engine.md, "File-list publication".
static constexpr std::chrono::milliseconds watchedChangesUpdateInterval{ 250 };

CPanel::CPanel(Panel position, CThreadPool& listingThreadPool, CThreadPool& workerThreadPool, qulonglong id, std::function<void()> wakeUiThread) :
	_panelPosition(position),
	_id(id),
	// The panel's own address is a unique, non-zero pool tag. Reuse-safe: ~CPanel retires all of this tag's tasks
	// before the address can be recycled by another CPanel, so a reused address never inherits stale tasks.
	_taskTag(reinterpret_cast<uint64_t>(this)),
	_listingThreadPool(listingThreadPool),
	_workerThreadPool(workerThreadPool),
	_wakeUiThread(std::move(wakeUiThread))
{
}

CPanel::~CPanel()
{
	// Cut short any in-flight background scan first, so the wait below is prompt instead of blocking until a
	// full recursive scan of the subtree completes.
	_abortBackgroundTasks = true;
	// Drop this panel's queued tasks from the shared pools and wait out any in-flight one,
	// so no task touches this panel's members after it's destroyed.
	_listingThreadPool.retire(_taskTag);
	_workerThreadPool.retire(_taskTag);
}

qulonglong CPanel::id() const noexcept
{
	return _id;
}

void CPanel::restoreHistory(const std::vector<QString>& history)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	_history.addLatest(history);
}

void CPanel::setActive(bool active)
{
	if (active)
	{
		CurrentDisplayMode displayMode;
		QString currentPath;
		{
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			_active = true;
			displayMode = _currentDisplayMode;
			currentPath = _currentDirObject.fullAbsolutePath();
		}

		// setPath comes first: there is nothing to watch or list until the tab has been pointed at a folder.
		assert_debug_only(!currentPath.isEmpty());

		watchView(currentPath, displayMode);
		refreshFileList();
	}
	else
	{
		{
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			_active = false;
		}

		_watcher.setPathToWatch({}); // Release the OS watch handle while inactive
	}
}

CPanel::FileListUpdateRequest CPanel::beginFileListUpdateLocked(CurrentDisplayMode displayMode)
{
	_currentDisplayMode = displayMode;
	if (displayMode != SearchResultsMode)
		_searchResultPaths.clear();

	FileListUpdateRequest request{ ++_fileListGeneration, _currentDirObject.fullAbsolutePath(), displayMode, _searchResultPaths,
		displayMode == AllObjectsMode ? std::make_shared<std::atomic<size_t>>(0) : nullptr };
	_filesFoundByPendingFlattenedListing = request.filesFound;
	_pendingWatchedChanges.clear(); // Made before the listing this starts, which reads the disk after them
	// The committed list now belongs to a view we've left, and the accessors hide it from here on, so the UI has to
	// be told to stop displaying it. Not a refresh: there are no contents to report until this update completes.
	// Nothing displays an inactive tab, and no listing would follow to lift the invalidation anyway.
	if (_active && !fileListBelongsToCurrentViewLocked())
		enqueueContentsInvalidatedNotificationLocked(request.generation);

	return request;
}

bool CPanel::fileListUpdateIsCurrentLocked(const FileListUpdateRequest& request) const
{
	return request.generation == _fileListGeneration && request.path == _currentDirObject.fullAbsolutePath() && request.displayMode == _currentDisplayMode;
}

bool CPanel::fileListBelongsToCurrentViewLocked() const
{
	return !_itemsSourcePath.isEmpty() && _itemsSourcePath == _currentDirObject.fullAbsolutePath() && _itemsSourceDisplayMode == _currentDisplayMode;
}

FileOperationResultCode CPanel::setPath(const QString &path, FileListRefreshCause operation)
{
#if defined _WIN32
	assert_r(!path.contains('\\'));
#endif

	bool active = false;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		active = _active;
	}

	// An inactive tab records the folder without reading the disk: setActive lists it, and a failed listing moves the tab
	// to an accessible folder. A path that is not absolute goes through the resolution below and lands on a fallback folder.
	if (!active)
	{
		if (CFileSystemObject folder = CFileSystemObject::unverifiedDirectory(path); folder.isValid())
		{
			moveToFolder({ .folder = std::move(folder), .listable = true, .isRequestedPathOrAncestor = true }, operation);
			return FileOperationResultCode::Ok;
		}
	}

	// Resolved before taking the list mutex: a check on an unreachable share waits out a network timeout.
	// The UI thread is the only writer of _history, so it reads it unlocked.
	ListableFolderProbe probe{ _abortBackgroundTasks };
	FolderResolution resolution = resolveFolder(path, currentDirObject(), _history, probe);
	const bool requestedPathSet = resolution.isRequestedPathOrAncestor;
	moveToFolder(std::move(resolution), operation);
	return requestedPathSet ? FileOperationResultCode::Ok : FileOperationResultCode::DirNotAccessible;
}

CPanel::FolderResolution CPanel::resolveFolder(const QString& path, const CFileSystemObject& currentDir, const CHistoryList<QString>& history, ListableFolderProbe& probe)
{
	FolderResolution resolution{ .listable = true };
	const auto landsOn = [&probe, &resolution](const QString& candidate) {
		if (!probe.isListable(candidate))
			return false;

		resolution.folder.setPath(candidate);
		return resolution.folder.isDir();
	};

	if (std::ranges::any_of(pathHierarchy(path), landsOn))
	{
		resolution.isRequestedPathOrAncestor = true;
		return resolution;
	}

	const QString& currentPath = currentDir.fullAbsolutePath();
	if (!currentPath.isEmpty() && landsOn(currentPath))
		return resolution;

	// rbegin() is the newest entry; step forward to the cursor, then keep walking towards the oldest.
	if (!history.empty() && std::any_of(history.rbegin() + (ptrdiff_t)(history.size() - 1 - history.currentIndex()), history.rend(), landsOn))
		return resolution;

	if (landsOn(QDir::homePath()))
		return resolution;

	// No folder can be listed: the panel keeps its folder (the requested one if it had none) and shows it empty
	resolution.listable = false;
	resolution.folder = currentPath.isEmpty() ? CFileSystemObject{ path } : currentDir;
	return resolution;
}

void CPanel::moveToFolder(FolderResolution resolution, FileListRefreshCause operation, const CurrentDisplayMode displayMode)
{
	std::unique_lock locker(_fileListAndCurrentDirMutex);

	const auto oldPathObject = _currentDirObject;
	const auto oldDisplayMode = _currentDisplayMode;
	const bool pathSet = resolution.isRequestedPathOrAncestor;

	_currentDirObject = std::move(resolution.folder);

	const QString newPath = _currentDirObject.fullAbsolutePath();

	// History management
#if defined _WIN32
	assert_r(!_history.currentItem().contains('\\') && !newPath.contains('\\'));
#endif
	if (_history.currentItem() != newPath)
		_history.addLatest(newPath);

	// Notify on an actual directory change. Keyed off the path changing, NOT off the history-add above: on
	// back/forward the history cursor has already moved, so addLatest is skipped, but the current dir still changed.
	// A fallback to history or home counts too: the panel moved even though the requested path couldn't be set.
	if (oldPathObject.fullAbsolutePath() != newPath)
		_currentPathChangedListeners.invokeCallback(&CurrentPathChangedListener::onCurrentPathChanged, _panelPosition, newPath);

	// An inactive tab neither watches nor lists its folder; setActive does both when the tab becomes the displayed one.
	const bool active = _active;
	if (active)
		watchView(newPath, displayMode);

	// Use the previous view's committed list to remember the selected child when navigating down.
	const auto newItemInPreviousFolder = _itemsSourcePath == oldPathObject.fullAbsolutePath() && _itemsSourceDisplayMode == oldDisplayMode ? _items.find(_currentDirObject.hash()) : _items.end();
	if (operation != refreshCauseCdUp && newItemInPreviousFolder != _items.end() && newItemInPreviousFolder->second.parentDirPath() != newItemInPreviousFolder->second.fullAbsolutePath())
		// Navigating downwards: the folder we're entering becomes the current item of the one we're leaving
		setCurrentItemHashForFolder(newItemInPreviousFolder->second.parentDirPath(), _currentDirObject.hash(), false);
	else if (operation == refreshCauseCdUp)
		// Navigating upwards: the folder we're leaving becomes the current item of the one we're entering
		setCurrentItemHashForFolder(_currentDirObject.fullAbsolutePath() /* where we are */, oldPathObject.hash() /* where we were */, false);

	// A fallback to another folder is not the navigation asked for
	beginNavigationLocked(pathSet ? operation : refreshCauseOther);
	const auto request = beginFileListUpdateLocked(displayMode);
	// Not listed: the listing would fail, and its recovery would find no folder either and end up here again
	if (!resolution.listable)
		publishFileListIfCurrent(request, {}, false);
	locker.unlock();

	if (active && resolution.listable)
		enqueueFileListUpdate(request);
}

void CPanel::beginNavigationLocked(FileListRefreshCause cause)
{
	++_navigationId;
	_navigationCause = cause;
	_navigationListingCommitted = false;
}

// Navigates up the directory tree
void CPanel::navigateUp()
{
	if (_currentDisplayMode != NormalMode)
		setPath(currentDirPathPosix(), refreshCauseOther);
	else
	{
		if (const auto parentDir = _currentDirObject.parentDirPath(); !parentDir.isEmpty())
			setPath(parentDir, refreshCauseCdUp);
		else
			sendContentsChangedNotification();
	}
}

// Go to the previous location from history
bool CPanel::navigateBack()
{
	if (_currentDisplayMode != NormalMode)
		return setPath(currentDirPathPosix(), refreshCauseOther) == FileOperationResultCode::Ok;

	return navigateToNearestAccessibleHistoryEntry(-1);
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (_currentDisplayMode != NormalMode)
		return false;

	return navigateToNearestAccessibleHistoryEntry(+1);
}

bool CPanel::navigateToNearestAccessibleHistoryEntry(const ptrdiff_t step)
{
	if (_history.empty())
		return false;

	ListableFolderProbe probe{ _abortBackgroundTasks };
	for (ptrdiff_t i = (ptrdiff_t)_history.currentIndex() + step; i >= 0 && i < (ptrdiff_t)_history.size(); i += step)
	{
		// The entry's own folder must be listable: setPath would land on the nearest accessible ancestor instead
		const QString path = _history[(size_t)i];
		if (!probe.isListable(path))
			continue;

		// Moved first, so that setPath finds the path already current and doesn't add it to the history again
		{
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			_history.setCurrentIndex((size_t)i);
		}
		return setPath(path, refreshCauseOther) == FileOperationResultCode::Ok;
	}

	return false;
}

const CHistoryList<QString>& CPanel::history() const
{
	return _history;
}

void CPanel::showAllFilesFromCurrentFolderAndBelow()
{
	FileListUpdateRequest request;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		beginNavigationLocked(refreshCauseOther);
		request = beginFileListUpdateLocked(AllObjectsMode);
	}

	// Before the listing starts: a change made before the watch exists may go unreported
	watchView(request.path, AllObjectsMode);
	enqueueFileListUpdate(request);
}

void CPanel::showSearchResults(QStringList paths)
{
	std::vector<QStringView> folders;
	folders.reserve((size_t)paths.size());
	for (const QString& path : paths)
		folders.push_back(parentFolderOf(path));

	// The UI thread is the only writer of _history, so it reads it unlocked
	ListableFolderProbe probe{ _abortBackgroundTasks };
	FolderResolution resolution = resolveFolder(deepestCommonFolder(folders), currentDirObject(), _history, probe);
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		_searchResultPaths = std::move(paths);
		// New results are a new view, even in the same folder: the committed list and its selection are no longer the view's
		_itemsSourcePath.clear();
		_selectedItemHashes.clear();
	}

	moveToFolder(std::move(resolution), refreshCauseOther, SearchResultsMode);
}

void CPanel::itemsRenamed(const std::vector<std::pair<QString, QString>>& renamedPaths)
{
	ankerl::unordered_dense::map<QString, QString, QStringHash, std::equal_to<>> newPathByOld;
	std::vector<const std::pair<QString, QString>*> renamedFolders;
	for (const auto& rename : renamedPaths)
	{
		newPathByOld.emplace(rename.first, rename.second);
		if (rename.first.endsWith('/'))
			renamedFolders.push_back(&rename);
	}

	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		for (QString& path : _searchResultPaths)
		{
			if (const auto renamed = newPathByOld.find(path); renamed != newPathByOld.end())
			{
				path = renamed->second;
				continue;
			}

			for (const auto* folder : renamedFolders)
			{
				if (path.startsWith(folder->first))
				{
					path.replace(0, folder->first.size(), folder->second);
					break;
				}
			}
		}
	}

	if (!watchKeepsViewCurrent())
		refreshFileList();
}

CPanel::CurrentDisplayMode CPanel::displayMode() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _currentDisplayMode;
}

bool CPanel::watchKeepsViewCurrent() const
{
	CurrentDisplayMode displayMode = NormalMode;
	bool treeHoldsFolderLinks = false;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		displayMode = _currentDisplayMode;
		treeHoldsFolderLinks = _listedTreeHoldsFolderLinks;
	}

	switch (displayMode)
	{
	case NormalMode:
		return true; // The poll covers the folder where notifications may miss a change
	case AllObjectsMode:
		return !treeHoldsFolderLinks && _watcher.notificationsReportEveryChange(); // The listing follows folder links, the watch does not
	case SearchResultsMode:
		return false;
	}

	assert_and_return_r(false, false);
}

// Switches to the appropriate directory and makes the specified item current
bool CPanel::goToItem(const CFileSystemObject& item)
{
	if (item.isCdUp() || !item.exists())
		return false;

	const QString dir = item.parentDirPath();
	// Placing the cursor is left to the refresh setPath() triggers, which reads this entry. A UI notification from
	// here would run before that refresh, against the folder we're leaving, and find nothing.
	setCurrentItemHashForFolder(dir, item.hash(), false);
	return setPath(dir, refreshCauseOther) == FileOperationResultCode::Ok;
}

CFileSystemObject CPanel::currentDirObject() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _currentDirObject;
}

// Info on the dir this panel is currently set to
QString CPanel::currentDirPathNative() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return toNativeSeparators(_currentDirObject.fullAbsolutePath());
}

QString CPanel::currentDirPathPosix() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _currentDirObject.fullAbsolutePath();
}

QString CPanel::currentDirName() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	const QString name = _currentDirObject.fullName().toString();
	return !name.isEmpty() ? name : _currentDirObject.fullAbsolutePath();
}

// One key per folder for the current-item map. Callers pass either a CFileSystemObject path or QDir::absolutePath(),
// both already absolute, POSIX-separated and free of duplicate separators - the trailing slash is the only thing
// that varies, and QDir::absolutePath() is the caller that omits it.
static QString folderKey(const QString& path)
{
	return withTrailingSeparator(path);
}

void CPanel::setCurrentItemHashForFolder(const QString& dir, qulonglong currentItemHash, const bool notifyUi)
{
#if defined _WIN32
	assert_r(!dir.contains('\\'));
#endif
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	_currentItemHashForFolder[folderKey(dir)] = currentItemHash;

	if (notifyUi)
	{
		// The caller is already on the UI thread, this is deferred invocation. Listeners must not run inside the
		// caller's stack: rename gets here from inside the delegate's commitData, where the view is still in
		// EditingState and would reject the cursor move, and setPath gets here under the mutex, mid-update.
		execOnUiThread([this, dir, currentItemHash]() {
			_currentItemChangedListeners.invokeCallback(&CurrentItemChangedListener::onCurrentItemChanged, _panelPosition, _id, dir, currentItemHash);
		});
	}
}

qulonglong CPanel::currentItemHashForFolder(const QString &dir) const
{
#if defined _WIN32
	assert_r(!dir.contains('\\'));
#endif
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	const auto it = _currentItemHashForFolder.find(folderKey(dir));
	return it == _currentItemHashForFolder.end() ? 0 : it->second;
}

CFileSystemObject CPanel::currentItem() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return currentItemLocked();
}

CFileSystemObject CPanel::currentItemLocked() const
{
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	const auto currentHash = _currentItemHashForFolder.find(folderKey(_currentDirObject.fullAbsolutePath()));
	if (currentHash == _currentItemHashForFolder.end())
		return {};

	const auto item = _items.find(currentHash->second);
	return item != _items.end() && _nameFilter.matches(item->second.fullName()) ? item->second : CFileSystemObject{};
}

void CPanel::setNameFilter(const QString& filterText)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	_nameFilterText = filterText;
	_nameFilter = FileNameFilter{ splitNameFilterField(filterText), Qt::CaseInsensitive };
	std::erase_if(_selectedItemHashes, [this](qulonglong hash) {
		const auto item = _items.find(hash);
		return item == _items.end() || !_nameFilter.matches(item->second.fullName());
	});
}

QString CPanel::nameFilter() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _nameFilterText;
}

void CPanel::setSorting(FileListSortField field, Qt::SortOrder order)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	_sortField = field;
	_sortOrder = order;
}

FileListSortField CPanel::sortField() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _sortField;
}

Qt::SortOrder CPanel::sortOrder() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _sortOrder;
}

bool CPanel::isSelectableLocked(const CFileSystemObject& item) const
{
	return !item.isCdUp() && _nameFilter.matches(item.fullName());
}

void CPanel::setItemsSelected(const std::vector<qulonglong>& hashes, bool selected)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!selected)
	{
		for (const qulonglong hash : hashes)
			_selectedItemHashes.erase(hash);
		return;
	}

	if (!fileListBelongsToCurrentViewLocked())
		return;

	for (const qulonglong hash : hashes)
	{
		if (const auto item = _items.find(hash); item != _items.end() && isSelectableLocked(item->second))
			_selectedItemHashes.insert(hash);
	}
}

void CPanel::invertSelection()
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return;

	for (const auto& [hash, item] : _items)
	{
		if (isSelectableLocked(item) && _selectedItemHashes.erase(hash) == 0)
			_selectedItemHashes.insert(hash);
	}
}

void CPanel::setItemsSelectedByPattern(const QString& patternText, const bool selected, const bool filesOnly)
{
	const FileNameFilter pattern{ splitNameFilterField(patternText), Qt::CaseInsensitive };
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return;

	for (const auto& [hash, item] : _items)
	{
		if (!isSelectableLocked(item) || (filesOnly && !isFileOrBundle(item)) || !pattern.matches(item.fullName()))
			continue;

		if (selected)
			_selectedItemHashes.insert(hash);
		else
			_selectedItemHashes.erase(hash);
	}
}

void CPanel::selectFilesWithExtensionOf(const qulonglong itemHash)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return;

	const auto sample = _items.find(itemHash);
	if (sample == _items.end() || !isFileOrBundle(sample->second))
		return;

	const QStringView extension = sample->second.extension();
	for (const auto& [hash, item] : _items)
	{
		if (isSelectableLocked(item) && isFileOrBundle(item) && item.extension().compare(extension, Qt::CaseInsensitive) == 0)
			_selectedItemHashes.insert(hash);
	}
}

std::vector<qulonglong> CPanel::selectedItemHashes() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	return _selectedItemHashes.values();
}

std::vector<CFileSystemObject> CPanel::selectedItems() const
{
	return selectedItemsInDisplayOrder(false);
}

std::vector<CFileSystemObject> CPanel::selectedItemsOrCurrentItem() const
{
	return selectedItemsInDisplayOrder(true);
}

std::vector<CFileSystemObject> CPanel::selectedItemsInDisplayOrder(bool orCurrentItem) const
{
	std::vector<CFileSystemObject> items;
	FileListSorter sorter;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		if (!fileListBelongsToCurrentViewLocked())
			return items;

		items.reserve(_selectedItemHashes.size());
		for (const qulonglong hash : _selectedItemHashes)
		{
			const auto item = _items.find(hash);
			assert_and_return_r(item != _items.end(), {});
			items.push_back(item->second);
		}

		if (items.empty() && orCurrentItem)
		{
			if (CFileSystemObject current = currentItemLocked(); current.isValid() && !current.isCdUp())
				items.push_back(std::move(current));
			return items;
		}

		sorter.setSorting(_sortField, _sortOrder);
		sorter.setListedFolder(_currentDirObject.fullAbsolutePath());
	}

	// Outside the lock: collation keys take long for a large selection
	sorter.sort(items);
	return items;
}

FolderContentsSummary CPanel::selectionSummary() const
{
	FolderContentsSummary summary;
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return summary;

	for (const qulonglong hash : _selectedItemHashes)
	{
		const auto item = _items.find(hash);
		assert_and_return_r(item != _items.end(), summary);
		summary.add(item->second);
	}

	return summary;
}

std::optional<FolderContentsSummary> CPanel::contentsSummary() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	FolderContentsSummary summary;
	for (const auto& [hash, item] : _items)
		summary.add(item);

	return summary;
}

QString CPanel::selectedOrCurrentItemPathLines() const
{
	std::vector<CFileSystemObject> items = selectedItems();
	if (items.empty())
	{
		if (CFileSystemObject current = currentItem(); current.isValid())
			items.push_back(std::move(current));
	}

	QString lines;
	for (const CFileSystemObject& item : items)
		lines += shellQuotedPath(toNativeSeparators(item.fullAbsolutePath())) + u'\n';
	lines.chop(1);
	return lines;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList()
{
	FileListUpdateRequest request;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		request = beginFileListUpdateLocked(_currentDisplayMode);
	}

	enqueueFileListUpdate(request);
}

void CPanel::enqueueFileListUpdate(FileListUpdateRequest request)
{
	_listingThreadPool.enqueue([this, request = std::move(request)]() {
		CTimeElapsed timer{ true };
		FileListHashMap items;
		bool listed = true;
		bool treeHoldsFolderLinks = false;
		const bool showHiddenFiles = QSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		// Baseline the folder before the enumeration below, so the watcher's snapshot matches this listing
		// (why: see captureBaselineState). Does nothing for search results, which have no watch.
		_watcher.captureBaselineState();

		if (request.displayMode == AllObjectsMode)
		{
			treeHoldsFolderLinks = addFlattenedListingItems(CFileSystemObject(request.path).fullAbsolutePath(), showHiddenFiles, items, _abortBackgroundTasks, request.filesFound.get());
			// The scan reports no failure: an unlistable root yields no files, and so does a tree that has none
			listed = !items.empty() || canListDirectory(request.path);
		}
		else if (request.displayMode == SearchResultsMode)
		{
			for (const QString& path : request.searchResultPaths)
			{
				if (_abortBackgroundTasks)
					return;

				if (CFileSystemObject item{ path }; item.exists())
					items.try_emplace(item.hash(), std::move(item));
			}
		}
		else if (auto listing = listDirectoryForPanel(request.path, showHiddenFiles))
			items = std::move(*listing);
		else
			listed = false;

		if (!listed)
		{
			recoverFromInaccessibleFolder(request);
			return;
		}

		if (const auto elapsedMs = timer.elapsed(); elapsedMs >= 100)
			qInfo() << "Listing" << request.path << "took" << elapsedMs << "ms for" << items.size() << "items";

		publishFileListIfCurrent(request, std::move(items), treeHoldsFolderLinks);
	}, _taskTag);
}

void CPanel::publishFileListIfCurrent(const FileListUpdateRequest& request, FileListHashMap&& items, const bool treeHoldsFolderLinks)
{
	std::unique_lock locker(_fileListAndCurrentDirMutex);
	if (!fileListUpdateIsCurrentLocked(request))
		return;

	const bool replacesWithdrawnList = !fileListBelongsToCurrentViewLocked();

	std::erase_if(_calculatedDirSizes, [&items](const auto& calculated) {
		const auto item = items.find(calculated.first);
		if (item == items.end() || !item->second.isDir())
			return true;

		item->second.setDirSize(calculated.second);
		return false;
	});

	// The path check: a hash the new listing gives to another item is not the selected one
	std::erase_if(_selectedItemHashes, [this, &items](qulonglong hash) {
		const auto newItem = items.find(hash);
		const auto oldItem = _items.find(hash);
		return newItem == items.end() || oldItem == _items.end() || newItem->second.fullAbsolutePath() != oldItem->second.fullAbsolutePath();
	});

	_items.swap(items);
	_itemsSourcePath = request.path;
	_itemsSourceDisplayMode = request.displayMode;
	_listedTreeHoldsFolderLinks = treeHoldsFolderLinks;
	_filesFoundByPendingFlattenedListing.reset();
	_itemsChangedSinceNotification.reset();
	_navigationListingCommitted = true;
	enqueueContentsChangedNotificationLocked(request.generation);
	locker.unlock();

	// A refresh of the same view never wakes the UI thread: the UI loop's wait paces repeated refills
	if (replacesWithdrawnList && _wakeUiThread)
		_wakeUiThread();
}

void CPanel::recoverFromInaccessibleFolder(const FileListUpdateRequest& request)
{
	std::unique_lock locker(_fileListAndCurrentDirMutex);
	if (!fileListUpdateIsCurrentLocked(request))
		return;

	const CHistoryList<QString> history = _history;
	const CFileSystemObject currentDir = _currentDirObject;
	locker.unlock();

	// Resolved as setPath(request.path) would, here rather than on the UI thread: each unreachable share costs a network timeout.
	// The failed listing already checked request.path.
	ListableFolderProbe probe{ _abortBackgroundTasks };
	probe.addUnlistable(request.path);
	FolderResolution resolution = resolveFolder(request.path, currentDir, history, probe);
	if (_abortBackgroundTasks)
		return;

	execOnUiThread([this, request, resolution = std::move(resolution)]() {
		{
			std::lock_guard uiThreadLocker(_fileListAndCurrentDirMutex);
			if (!fileListUpdateIsCurrentLocked(request))
				return;
		}

		// Not a user-requested navigation
		moveToFolder(resolution, refreshCauseOther);
	});
}

void CPanel::readCommittedContents(const std::function<void(const QString&, const FileListHashMap&)>& fn) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return;

	fn(_currentDirObject.fullAbsolutePath(), _items);
}

CPanel::CommittedListing CPanel::committedListing() const
{
	CommittedListing listing;
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return listing;

	listing.isCommitted = true;
	listing.folder = _currentDirObject.fullAbsolutePath();
	listing.items.reserve(_items.size());
	for (const auto& [hash, item] : _items)
		listing.items.push_back(item);

	return listing;
}

std::optional<std::vector<std::pair<qulonglong, uint64_t>>> CPanel::dirSizesBeingCalculated() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (_dirSizeCalculationsQueued == 0 && _dirSizesBeingCalculated.empty())
		return {};

	std::vector<std::pair<qulonglong, uint64_t>> sizes;
	sizes.reserve(_dirSizesBeingCalculated.size());
	for (const auto& [hash, occupiedSpaceSoFar] : _dirSizesBeingCalculated)
		sizes.emplace_back(hash, occupiedSpaceSoFar->load(std::memory_order_relaxed));
	return sizes;
}

std::optional<size_t> CPanel::filesFoundByPendingFlattenedListing() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!_filesFoundByPendingFlattenedListing)
		return {};

	return _filesFoundByPendingFlattenedListing->load(std::memory_order_relaxed);
}

uint64_t CPanel::navigationId() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return _navigationId;
}

bool CPanel::itemHashExists(const qulonglong hash) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return fileListBelongsToCurrentViewLocked() && _items.contains(hash);
}

CFileSystemObject CPanel::itemByHash(qulonglong hash) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	const auto it = _items.find(hash);
	return it != _items.end() ? it->second : CFileSystemObject();
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::displayDirSize(qulonglong dirHash)
{
	// Nothing to do if not a dir
	const auto item = itemByHash(dirHash);
	if (!item.isDir())
		return;

	{
		std::lock_guard locker{ _fileListAndCurrentDirMutex };
		++_dirSizeCalculationsQueued;
	}
	_workerThreadPool.enqueue([this, dirHash, path = item.fullAbsolutePath()] {
		std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
		{
			std::lock_guard locker{ _fileListAndCurrentDirMutex };
			--_dirSizeCalculationsQueued;
			_dirSizesBeingCalculated[dirHash] = &occupiedSpaceSoFar;
		}
		// Also on a throw, which the pool contains: the map must not keep a pointer into this frame
		EXEC_ON_SCOPE_EXIT([this, dirHash, &occupiedSpaceSoFar] {
			std::lock_guard locker{ _fileListAndCurrentDirMutex };
			// Not when a later calculation of the same folder registered its own counter
			if (const auto registered = _dirSizesBeingCalculated.find(dirHash); registered != _dirSizesBeingCalculated.end() && registered->second == &occupiedSpaceSoFar)
				_dirSizesBeingCalculated.erase(registered);
		});

		const FileStatistics stats = calculateStatsFor({ path }, _abortBackgroundTasks, {}, &occupiedSpaceSoFar);

		// Since this is a background thread, the item we were working on may be out of the _items list by now.
		// So we find it again and see if it's still there.
		{
			std::lock_guard locker{ _fileListAndCurrentDirMutex };
			if (!fileListBelongsToCurrentViewLocked())
				return;

			const auto it = _items.find(dirHash);
			if (it == _items.end())
				return;

			it->second.setDirSize(stats.occupiedSpace);
			_calculatedDirSizes[dirHash] = stats.occupiedSpace;
			commitItemChangesLocked({ dirHash });
		}
	}, _taskTag);
}

void CPanel::sendContentsChangedNotification()
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	enqueueContentsChangedNotificationLocked(_fileListGeneration);
}

void CPanel::commitItemChangesLocked(const std::vector<qulonglong>& hashes)
{
	assert_debug_only(fileListBelongsToCurrentViewLocked());

	if (_itemsChangedSinceNotification)
	{
		_itemsChangedSinceNotification->insert(hashes.cbegin(), hashes.cend());
		// Past this many, the GUI refills faster than it applies the changes: see core-engine.md, "File-list publication"
		static constexpr size_t maxItemChangesPerNotification = 700;
		if (_itemsChangedSinceNotification->size() > maxItemChangesPerNotification)
			_itemsChangedSinceNotification.reset();
	}

	for (const qulonglong hash : hashes)
	{
		if (!_items.contains(hash))
			_selectedItemHashes.erase(hash);
	}

	enqueueContentsChangedNotificationLocked(_fileListGeneration);
}

PanelContentsChange CPanel::takeContentsChangeLocked()
{
	PanelContentsChange change;
	// A notification sent while the view's listing is pending finds the previous view's items
	if (!_itemsChangedSinceNotification || !fileListBelongsToCurrentViewLocked())
	{
		_itemsChangedSinceNotification.emplace();
		return change;
	}

	change.full = false;
	for (const qulonglong hash : *_itemsChangedSinceNotification)
	{
		if (const auto item = _items.find(hash); item != _items.end())
			change.upserted.push_back(item->second);
		else
			change.removedHashes.push_back(hash);
	}
	_itemsChangedSinceNotification->clear();
	return change;
}

void CPanel::enqueueContentsChangedNotificationLocked(uint64_t generation)
{
	// The change and the cause are taken when the notification runs: one coalesced notification delivers everything committed before it
	execOnUiThread([this, generation]() {
		PanelContentsChange change;
		FileListRefreshCause cause = refreshCauseOther;
		{
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			if (generation != _fileListGeneration)
				return;

			change = takeContentsChangeLocked();
			if (_navigationListingCommitted)
				cause = std::exchange(_navigationCause, refreshCauseOther);
		}

		_panelContentsChangedListeners.invokeCallback(&PanelContentsChangedListener::onPanelContentsChanged, _panelPosition, _id, cause, std::move(change));
	}, ContentsChangedNotificationTag);
}

// Shares ContentsChangedNotificationTag with the notification above: when the update completes within the same UI
// queue drain, the contents-changed notification replaces this one and the view never blanks.
void CPanel::enqueueContentsInvalidatedNotificationLocked(uint64_t generation) const
{
	execOnUiThread([this, generation]() {
		if (!fileListGenerationIsCurrent(generation))
			return;

		_panelContentsChangedListeners.invokeCallback(&PanelContentsChangedListener::onPanelContentsInvalidated, _panelPosition, _id);
	}, ContentsChangedNotificationTag);
}

bool CPanel::fileListGenerationIsCurrent(uint64_t generation) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	return generation == _fileListGeneration;
}

void CPanel::uiThreadTimerTick()
{
	_uiThreadQueue.exec();
	applyWatchedChanges();
}

void CPanel::addPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	_panelContentsChangedListeners.addSubscriber(listener);
}

void CPanel::addCurrentItemChangedListener(CurrentItemChangedListener * listener)
{
	_currentItemChangedListeners.addSubscriber(listener);
}

void CPanel::addCurrentPathChangedListener(CurrentPathChangedListener * listener)
{
	_currentPathChangedListeners.addSubscriber(listener);
}

void CPanel::watchView(const QString& path, const CurrentDisplayMode displayMode)
{
	_watcher.setPathToWatch(displayMode == SearchResultsMode ? QString{} : path, displayMode == AllObjectsMode);
}

void CPanel::applyWatchedChanges()
{
	if (displayMode() != AllObjectsMode)
	{
		if (_watcher.changesDetected())
			refreshFileList();
		return;
	}

	CFileSystemWatcher::Changes changes = _watcher.takeChanges();
	if (changes.rescan)
	{
		refreshFileList();
		return;
	}

	std::lock_guard locker(_fileListAndCurrentDirMutex);
	for (CFileSystemWatcher::ChangedPath& change : changes.paths)
	{
		const auto [pending, inserted] = _pendingWatchedChanges.try_emplace(change.path, change);
		if (!inserted)
		{
			pending->second.contentsMayBeNew |= change.contentsMayBeNew;
			pending->second.contentsMayBeGone |= change.contentsMayBeGone;
		}
	}

	if (watchedChangesUpdateCanStartLocked())
		startWatchedChangesUpdateLocked();
}

bool CPanel::watchedChangesUpdateCanStartLocked() const
{
	// The current request's filesFound: a flattened listing is pending until it publishes, and its commit triggers no update
	return !_pendingWatchedChanges.empty() && !_watchedChangesUpdateRunning && fileListBelongsToCurrentViewLocked() && !_filesFoundByPendingFlattenedListing
		&& std::chrono::steady_clock::now() - _watchedChangesUpdateStart >= watchedChangesUpdateInterval;
}

void CPanel::startWatchedChangesUpdateLocked()
{
	std::vector<CFileSystemWatcher::ChangedPath> changes;
	changes.reserve(_pendingWatchedChanges.size());
	for (auto& [path, change] : _pendingWatchedChanges)
		changes.push_back(std::move(change));
	_pendingWatchedChanges.clear();

	_watchedChangesUpdateRunning = true;
	_watchedChangesUpdateStart = std::chrono::steady_clock::now();
	_listingThreadPool.enqueue([this, generation = _fileListGeneration, changes = std::move(changes)] {
		EXEC_ON_SCOPE_EXIT([this] {
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			_watchedChangesUpdateRunning = false;
		});

		const bool showHiddenFiles = QSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		FlattenedListingRecheck recheck = recheckFlattenedListing(changes, showHiddenFiles, _abortBackgroundTasks);
		if (_abortBackgroundTasks)
			return;

		{
			std::lock_guard locker(_fileListAndCurrentDirMutex);
			// A listing begun since then reads the disk after these changes, and may read it after this recheck did
			if (generation != _fileListGeneration || !fileListBelongsToCurrentViewLocked())
				return;

			if (!recheck.listAgain)
			{
				applyFlattenedListingRecheckLocked(std::move(recheck));
				return;
			}
		}

		refreshFileList();
	}, _taskTag);
}

void CPanel::applyFlattenedListingRecheckLocked(FlattenedListingRecheck&& recheck)
{
	// A listed file at the path means no folder left it
	std::erase_if(recheck.vanishedFolderCandidates, [this](const QString& folder) { return _items.contains(pathHash(folder.chopped(1))); });
	std::vector<qulonglong> changedHashes = removeItemsUnderFolders(_items, std::move(recheck.vanishedFolderCandidates), recheck.upserted);
	for (const qulonglong hash : recheck.removedHashes)
	{
		if (_items.erase(hash) != 0)
			changedHashes.push_back(hash);
	}

	for (auto& [hash, item] : recheck.upserted)
	{
		changedHashes.push_back(hash);
		_items.insert_or_assign(hash, std::move(item));
	}

	if (!changedHashes.empty())
		commitItemChangesLocked(changedHashes);
}
