#include "cpanel.h"
#include "settings/csettings.h"
#include "settings.h"
#include "filesystemhelperfunctions.h"
#include "directoryscanner.h"
#include "assert/advanced_assert.h"
#include "std_helpers/qt_container_helpers.hpp"
#include "filesystemhelpers/filestatistics.h"
#include "filesystemhelpers/filesystemhelpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm> // std::min
#include <time.h>


enum {
	ContentsChangedNotificationTag,
	ItemDiscoveryProgressNotificationTag
};

CPanel::CPanel(Panel position, CWorkerThreadPool& workerThreadPool, qulonglong id) :
	_panelPosition(position),
	_id(id),
	// The panel's own address is a unique, non-zero pool tag. Reuse-safe: ~CPanel retires all of this tag's tasks
	// before the address can be recycled by another CPanel, so a reused address never inherits stale tasks.
	_taskTag(reinterpret_cast<uint64_t>(this)),
	_workerThreadPool(workerThreadPool)
{
}

CPanel::~CPanel()
{
	// Cut short any in-flight background scan first, so the wait below is prompt instead of blocking until a
	// full recursive scan of the subtree completes.
	_abortBackgroundTasks = true;
	// Drop this panel's queued tasks from the shared pool and wait out any in-flight one,
	// so no task touches this panel's members after it's destroyed.
	_workerThreadPool.retire(_taskTag);
}

qulonglong CPanel::id() const noexcept
{
	return _id;
}

void CPanel::restoreHistory(const std::vector<QString>& history)
{
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
			displayMode = _currentDisplayMode;
			currentPath = _currentDirObject.fullAbsolutePath();
		}

		// Flattened mode has no watcher because a single-directory watcher cannot represent its recursive contents.
		_watcher.setPathToWatch(displayMode == NormalMode ? currentPath : QString{});
		refreshFileList(refreshCauseOther);
	}
	else
		_watcher.setPathToWatch({}); // Release the OS watch handle while inactive
}

CPanel::FileListUpdateRequest CPanel::beginFileListUpdateLocked(CurrentDisplayMode displayMode)
{
	_currentDisplayMode = displayMode;
	FileListUpdateRequest request{ ++_fileListGeneration, _currentDirObject.fullAbsolutePath(), displayMode };
	// The committed list now belongs to a view we've left, and the accessors hide it from here on, so the UI has to
	// be told to stop displaying it. Not a refresh: there are no contents to report until this update completes.
	if (!fileListBelongsToCurrentViewLocked())
		enqueueContentsInvalidatedNotificationLocked(request.generation);

	return request;
}

bool CPanel::fileListUpdateIsCurrentLocked(const FileListUpdateRequest& request) const
{
	return request.generation == _fileListGeneration && request.path == _currentDirObject.fullAbsolutePath() && request.displayMode == _currentDisplayMode;
}

bool CPanel::fileListBelongsToCurrentViewLocked() const
{
	return _itemsSourcePath == _currentDirObject.fullAbsolutePath() && _itemsSourceDisplayMode == _currentDisplayMode;
}

FileOperationResultCode CPanel::setPath(const QString &path, FileListRefreshCause operation)
{
#if defined _WIN32
	assert_r(!path.contains('\\'));
#endif

	std::unique_lock locker(_fileListAndCurrentDirMutex);

	const auto oldPathObject = _currentDirObject;
	const auto oldDisplayMode = _currentDisplayMode;

	bool pathSet = false;
	for (auto&& candidatePath: pathHierarchy(path))
	{
		if (pathIsAccessible(candidatePath))
		{
			_currentDirObject.setPath(candidatePath);
			if (_currentDirObject.isDir())
			{
				pathSet = true;
				break;
			}
		}
	}

	if (!pathSet)
	{
		if (pathIsAccessible(oldPathObject.fullAbsolutePath()))
			_currentDirObject.setPath(oldPathObject.fullAbsolutePath());
		else
		{
			QString pathToSet;
			for (auto it = history().rbegin() + ((ptrdiff_t)history().size() - 1 - (ptrdiff_t)history().currentIndex()); it != history().rend(); ++it)
			{
				if (pathIsAccessible(*it))
				{
					pathToSet = *it;
					break;
				}
			}

			if (pathToSet.isEmpty())
				pathToSet = QDir::homePath();
			_currentDirObject.setPath(pathToSet);
		}
	}

	const QString newPath = _currentDirObject.fullAbsolutePath();

	// History management
#if defined _WIN32
	assert_r(!_history.currentItem().contains('\\') && !newPath.contains('\\'));
#endif
	if (_history.currentItem() != newPath)
		_history.addLatest(newPath);

	// Notify on an actual directory change. Keyed off the path changing, NOT off the history-add above: on
	// back/forward the history cursor has already moved, so addLatest is skipped, but the current dir still changed.
	if (pathSet && oldPathObject.fullAbsolutePath() != newPath)
		_currentPathChangedListeners.invokeCallback(&CurrentPathChangedListener::onCurrentPathChanged, _panelPosition, newPath);

	if (_watcher.setPathToWatch(newPath) == false)
		qInfo() << __FUNCTION__ << "Error setting path" << newPath << "to CFileSystemWatcher";

	// Use the previous view's committed list to remember the selected child when navigating down.
	const auto newItemInPreviousFolder = _itemsSourcePath == oldPathObject.fullAbsolutePath() && _itemsSourceDisplayMode == oldDisplayMode ? _items.find(_currentDirObject.hash()) : _items.end();
	if (operation != refreshCauseCdUp && newItemInPreviousFolder != _items.end() && newItemInPreviousFolder->second.parentDirPath() != newItemInPreviousFolder->second.fullAbsolutePath())
		// Navigating downwards: the folder we're entering becomes the current item of the one we're leaving
		setCurrentItemHashForFolder(newItemInPreviousFolder->second.parentDirPath(), _currentDirObject.hash(), false);
	else if (operation == refreshCauseCdUp)
		// Navigating upwards: the folder we're leaving becomes the current item of the one we're entering
		setCurrentItemHashForFolder(_currentDirObject.fullAbsolutePath() /* where we are */, oldPathObject.hash() /* where we were */, false);

	const auto request = beginFileListUpdateLocked(NormalMode);
	locker.unlock();

	enqueueFileListUpdate(request, pathSet ? operation : refreshCauseOther);
	return pathSet ? FileOperationResultCode::Ok : FileOperationResultCode::DirNotAccessible;
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
			sendContentsChangedNotification(refreshCauseOther);
	}
}

// Go to the previous location from history
bool CPanel::navigateBack()
{
	if (_currentDisplayMode != NormalMode)
		return setPath(currentDirPathPosix(), refreshCauseOther) == FileOperationResultCode::Ok;

	if (_history.empty())
		return false;
	
	while (!_history.isAtBeginning())
	{
		const QString path = _history.navigateBack();
		if (pathIsAccessible(path))
		{
			return setPath(path, refreshCauseOther) == FileOperationResultCode::Ok;
		}
	}

	return false;
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (_currentDisplayMode != NormalMode || _history.empty() || _history.isAtEnd())
		return false;

	const QString path = _history.navigateForward();
	return setPath(path, refreshCauseOther) == FileOperationResultCode::Ok;
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
		request = beginFileListUpdateLocked(AllObjectsMode);
	}

	_watcher.setPathToWatch({});
	enqueueFileListUpdate(request, refreshCauseOther);
}

// Switches to the appropriate directory and makes the specified item current
bool CPanel::goToItem(const CFileSystemObject& item)
{
	if (!item.exists())
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
	const QString name = _currentDirObject.fullName();
	return !name.isEmpty() ? name : _currentDirObject.fullAbsolutePath();
}

// One key per folder for the current-item map. Callers pass either a CFileSystemObject path or QDir::absolutePath(),
// both already absolute, POSIX-separated and free of duplicate separators - the trailing slash is the only thing
// that varies, and QDir::absolutePath() is the caller that omits it.
static QString folderKey(const QString& path)
{
	return path.endsWith('/') ? path : path + '/';
}

void CPanel::setCurrentItemHashForFolder(const QString& dir, qulonglong currentItemHash, const bool notifyUi)
{
#if defined _WIN32
	assert_r(!dir.contains('\\'));
#endif
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
	const auto it = _currentItemHashForFolder.find(folderKey(dir));
	return it == _currentItemHashForFolder.end() ? 0 : it->second;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList(FileListRefreshCause operation)
{
	FileListUpdateRequest request;
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		request = beginFileListUpdateLocked(_currentDisplayMode);
	}

	enqueueFileListUpdate(request, operation);
}

void CPanel::enqueueFileListUpdate(FileListUpdateRequest request, FileListRefreshCause operation)
{
	_workerThreadPool.enqueue([this, request = std::move(request), operation]() {
		if (!pathIsAccessible(request.path))
		{
			execOnUiThread([this, request]() { recoverFromInaccessiblePathIfCurrent(request); });
			return;
		}

		FileListHashMap items;
		const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();

		if (request.displayMode == AllObjectsMode)
		{
			scanDirectory(CFileSystemObject(request.path), [&items, showHiddenFiles](const CFileSystemObject& item, bool /*reachedThroughLink*/) {
				if (item.isFile() && item.exists() && (showHiddenFiles || !item.isHidden()))
					items[item.hash()] = item;
			}, _abortBackgroundTasks);
		}
		else
		{
			const QFileInfoList directoryEntries = QDir{request.path}.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
			for (const QFileInfo& directoryEntry : directoryEntries)
			{
#ifndef _WIN32
				// The root's ".." entry is itself (/.. == /); skip it so the root listing has no self-referential parent row.
				// Only the filesystem root yields this exact path. (Windows roots don't produce it.)
				if (directoryEntry.absoluteFilePath() == QLatin1String("/.."))
					continue;
#endif

				CFileSystemObject object(directoryEntry);
				if ((!object.isFile() && !object.isDir()) || !object.exists() || (!showHiddenFiles && object.isHidden()))
					continue; // Could be a socket

				const qulonglong hash = object.hash();
				items[hash] = std::move(object);
			}
		}

		publishFileListIfCurrent(request, std::move(items), operation);
	}, _taskTag);
}

void CPanel::publishFileListIfCurrent(const FileListUpdateRequest& request, FileListHashMap&& items, FileListRefreshCause operation)
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListUpdateIsCurrentLocked(request))
		return;

	_items.swap(items);
	_itemsSourcePath = request.path;
	_itemsSourceDisplayMode = request.displayMode;
	enqueueContentsChangedNotificationLocked(operation, request.generation);
}

void CPanel::recoverFromInaccessiblePathIfCurrent(const FileListUpdateRequest& request)
{
	{
		std::lock_guard locker(_fileListAndCurrentDirMutex);
		if (!fileListUpdateIsCurrentLocked(request))
			return;
	}

	// Deliberately the path we just found inaccessible: setPath finds the closest accessible folder to land on
	// instead. This is not a user-requested navigation, so refreshCauseOther.
	setPath(request.path, refreshCauseOther);
}

// Returns the current list of objects on this panel
FileListHashMap CPanel::list() const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	return _items;
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

QString CPanel::itemPathByHash(qulonglong hash) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return {};

	const auto it = _items.find(hash);
	return it != _items.end() ? it->second.fullAbsolutePath() : QString();
}

std::vector<QString> CPanel::itemPathsByHashes(const std::vector<qulonglong>& hashes) const
{
	std::vector<QString> paths;
	paths.reserve(hashes.size());

	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return std::vector<QString>(hashes.size());

	for (const auto hash : hashes)
	{
		const auto it = _items.find(hash);
		if (it != _items.end())
			paths.push_back(it->second.fullAbsolutePath());
		else
			paths.push_back({});
	}

	return paths;
}

std::vector<qulonglong> CPanel::itemHashes() const
{
	std::vector<qulonglong> hashes;

	std::lock_guard locker(_fileListAndCurrentDirMutex);
	if (!fileListBelongsToCurrentViewLocked())
		return hashes;

	hashes.reserve(_items.size());

	for (const auto& pair : _items)
		hashes.push_back(pair.first);

	return hashes;
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::displayDirSize(qulonglong dirHash)
{
	// Nothing to do if not a dir
	const auto item = itemByHash(dirHash);
	if (!item.isDir())
		return;

	_workerThreadPool.enqueue([this, dirHash, path = item.fullAbsolutePath()] {
		const FileStatistics stats = calculateStatsFor({ path }, _abortBackgroundTasks);

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
			enqueueContentsChangedNotificationLocked(refreshCauseOther, _fileListGeneration);
		}
	}, _taskTag);
}

void CPanel::sendContentsChangedNotification(FileListRefreshCause operation) const
{
	std::lock_guard locker(_fileListAndCurrentDirMutex);
	enqueueContentsChangedNotificationLocked(operation, _fileListGeneration);
}

void CPanel::enqueueContentsChangedNotificationLocked(FileListRefreshCause operation, uint64_t generation) const
{
	execOnUiThread([this, operation, generation]() {
		if (!fileListGenerationIsCurrent(generation))
			return;

		_panelContentsChangedListeners.invokeCallback(&PanelContentsChangedListener::onPanelContentsChanged, _panelPosition, _id, operation);
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
	refreshIfWatcherDetectedChanges();
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

bool CPanel::pathIsAccessible(const QString& path) const
{
	return FileSystemHelpers::pathIsAccessible(path);
}

void CPanel::refreshIfWatcherDetectedChanges()
{
	if (_watcher.changesDetected())
		refreshFileList(refreshCauseOther);
}
