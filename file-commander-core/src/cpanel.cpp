#include "cpanel.h"
#include "settings/csettings.h"
#include "settings.h"
#include "filesystemhelperfunctions.h"
#include "directoryscanner.h"
#include "assert/advanced_assert.h"
#include "filesystemwatcher/cfilesystemwatcher.h"
#include "std_helpers/qt_container_helpers.hpp"
#include "filesystemhelpers/filesystemhelpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QVector>
RESTORE_COMPILER_WARNINGS

#include <time.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h> // access()
#endif

enum {
	ContentsChangedNotificationTag,
	ItemDiscoveryProgressNotificationTag
};

CPanel::CPanel(Panel position) :
	_watcher(std::make_shared<CFileSystemWatcher>()),
	_panelPosition(position),
	_workerThreadPool(4, std::string(position == LeftPanel ? "Left panel" : "Right panel") + " file list refresh thread pool")
{
	// The list of items in the current folder is being refreshed asynchronously, not every time a change is detected, to avoid refresh tasks queuing up out of control
	_fileListRefreshTimer.start(200);
	connect(&_fileListRefreshTimer, &QTimer::timeout, this, &CPanel::processContentsChangedEvent);

	_watcher->addCallback([this](const transparent_set<QFileInfo>&, const transparent_set<QFileInfo>&, const transparent_set<QFileInfo>&) {
		contentsChanged();
	});
}

void CPanel::restoreFromSettings()
{
	CSettings s;
	const QStringList historyList = s.value(_panelPosition == RightPanel ? KEY_HISTORY_R : KEY_HISTORY_L).toStringList();
	_history.addLatest(to_vector<QString>(historyList));
	setPath(s.value(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::homePath()).toString(), refreshCauseOther);
}

FileOperationResultCode CPanel::setPath(const QString &path, FileListRefreshCause operation)
{
#if defined _WIN32
	assert_r(!path.contains('\\'));
#endif

	_currentDisplayMode = NormalMode;

	std::unique_lock<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

	const auto oldPathObject = _currentDirObject;

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
			for (auto it = history().rbegin() + (history().size() - 1 - history().currentIndex()); it != history().rend(); ++it)
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
	assert_r(!_history.currentItem().contains('\\') && !newPath.contains('\\'));
	if (_history.empty())
		// The current folder does not automatically make it into history on program startup, but it should (#103)
		_history.addLatest(oldPathObject.fullAbsolutePath());

	CSettings settings;
	if (_history.currentItem() != newPath)
	{
		_history.addLatest(newPath);
		settings.setValue(_panelPosition == RightPanel ? KEY_HISTORY_R : KEY_HISTORY_L, QVariant(QStringList(_history.list().cbegin(), _history.list().cend())));
	}

	settings.setValue(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, newPath);

	if (_watcher->setPathToWatch(newPath) == false)
		qInfo() << __FUNCTION__ << "Error setting path" << newPath << "to CFileSystemWatcher";

	// If the new folder is one of the subfolders of the previous folder, mark it as the current for that previous folder
	// We're using the fact that _currentDirObject is already updated, but the _items list is not as it still corresponds to the previous location
	const auto newItemInPreviousFolder = _items.find(_currentDirObject.hash());
	if (operation != refreshCauseCdUp && newItemInPreviousFolder != _items.end() && newItemInPreviousFolder->second.parentDirPath() != newItemInPreviousFolder->second.fullAbsolutePath())
		// Updating the cursor when navigating downwards
		setCurrentItemForFolder(newItemInPreviousFolder->second.parentDirPath(), _currentDirObject.hash(), false);
	else if (operation == refreshCauseCdUp)
		// Updating the cursor when navigating upwards
		setCurrentItemForFolder(_currentDirObject.fullAbsolutePath() /* where we are */, oldPathObject.hash() /* where we were */, false);

	locker.unlock();

	refreshFileList(pathSet ? operation : refreshCauseOther);
	return pathSet ? FileOperationResultCode::Ok : FileOperationResultCode::DirNotAccessible;
}

// Navigates up the directory tree
void CPanel::navigateUp()
{
	if (_currentDisplayMode != NormalMode)
		setPath(currentDirPathPosix(), refreshCauseOther);
	else
	{
		if (!_currentDirObject.parentDirPath().isEmpty())
			setPath(_currentDirObject.parentDirPath(), refreshCauseCdUp);
		else
			sendContentsChangedNotification(refreshCauseOther);
	}
}

// Go to the previous location from history
bool CPanel::navigateBack()
{
	if (_currentDisplayMode != NormalMode)
		return setPath(currentDirPathPosix(), refreshCauseOther) == FileOperationResultCode::Ok;
	else if (!_history.empty())
		return setPath(_history.navigateBack(), refreshCauseOther) == FileOperationResultCode::Ok;
	else
		return false;
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (_currentDisplayMode == NormalMode && !_history.empty())
		return setPath(_history.navigateForward(), refreshCauseOther) == FileOperationResultCode::Ok;
	return false;
}

const CHistoryList<QString>& CPanel::history() const
{
	return _history;
}

void CPanel::showAllFilesFromCurrentFolderAndBelow()
{
	_currentDisplayMode = AllObjectsMode;
	_watcher->setPathToWatch(QString());

	_workerThreadPool.enqueue([this]() {
		std::unique_lock<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
		const QString path = _currentDirObject.fullAbsolutePath();

		_items.clear();

		//locker.unlock();
		// TODO: synchronization and lock-ups
		const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		scanDirectory(CFileSystemObject(path), [showHiddenFiles, this](const CFileSystemObject& item) {
			if (item.isFile() && item.exists() && (showHiddenFiles || !item.isHidden()))
				_items[item.hash()] = item;
		});
		//locker.lock();

		sendContentsChangedNotification(refreshCauseOther);
	});
}

// Switches to the appropriate directory and sets the cursor to the specified item
bool CPanel::goToItem(const CFileSystemObject& item)
{
	if (!item.exists())
		return false;

	const QString dir = item.parentDirPath();
	setCurrentItemForFolder(dir, item.hash());
	return setPath(dir, refreshCauseOther) == FileOperationResultCode::Ok;
}

CFileSystemObject CPanel::currentDirObject() const
{
	return _currentDirObject;
}

// Info on the dir this panel is currently set to
QString CPanel::currentDirPathNative() const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
	return toNativeSeparators(_currentDirObject.fullAbsolutePath());
}

QString CPanel::currentDirPathPosix() const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
	return _currentDirObject.fullAbsolutePath();
}

QString CPanel::currentDirName() const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
	const QString name = _currentDirObject.fullName();
	return !name.isEmpty() ? name : _currentDirObject.fullAbsolutePath();
}

inline QString normalizeFolderPath(const QString& path)
{
	const QString posixPath = cleanPath(path);
	return posixPath.endsWith('/') ? posixPath : (posixPath + '/');
}

void CPanel::setCurrentItemForFolder(const QString& dir, qulonglong currentItemHash, const bool notifyUi)
{
	assert_r(!dir.contains('\\'));
	_cursorPosForFolder[normalizeFolderPath(dir)] = currentItemHash;

	if (notifyUi)
	{
		execOnUiThread([this, dir, currentItemHash]() {
			_currentItemChangeListener.invokeCallback(&CursorPositionListener::setCursorToItem, dir, currentItemHash);
		});
	}
}

qulonglong CPanel::currentItemForFolder(const QString &dir) const
{
	assert_r(!dir.contains('\\'));
	const auto it = _cursorPosForFolder.find(normalizeFolderPath(dir));
	return it == _cursorPosForFolder.end() ? 0 : it->second;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList(FileListRefreshCause operation)
{
	_workerThreadPool.enqueue([this, operation]() {
		QFileInfoList list;

		bool currentPathIsAccessible = false;
		QString currentDirPath;

		{
			std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

			currentDirPath = _currentDirObject.fullAbsolutePath();
			currentPathIsAccessible = pathIsAccessible(currentDirPath);
		}

		if (!currentPathIsAccessible)
		{
			setPath(currentDirPath, operation); // setPath will itself find the closest best folder to set instead
			return;
		}

		{
			std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

			list = QDir{currentDirPath}.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
			_items.clear();
		}

		const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		std::vector<CFileSystemObject> objectsList;

		const size_t numItemsFound = (size_t)list.size();
		objectsList.reserve(numItemsFound);

		for (size_t i = 0; i < numItemsFound; ++i)
		{
#ifndef _WIN32
			// TODO: Qt bug?
			if (list[(int)i].absoluteFilePath() == QLatin1String("/.."))
				continue;
#endif
			objectsList.emplace_back(list[(int)i]);
			if (!objectsList.back().isFile() && !objectsList.back().isDir())
				objectsList.pop_back(); // Could be a socket

			sendItemDiscoveryProgressNotification(_currentDirObject.hash(), 20 + 80 * i / numItemsFound, _currentDirObject.fullAbsolutePath());
		}

		{
			std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

			for (const auto& object : objectsList)
			{
				if (object.exists() && (showHiddenFiles || !object.isHidden()))
					_items[object.hash()] = object;
			}
		}

		sendContentsChangedNotification(operation);
	});
}

// Returns the current list of objects on this panel
std::map<qulonglong, CFileSystemObject> CPanel::list() const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
	return _items;
}

bool CPanel::itemHashExists(const qulonglong hash) const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
	return _items.count(hash) > 0;
}

CFileSystemObject CPanel::itemByHash(qulonglong hash) const
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

	const auto it = _items.find(hash);
	return it != _items.end() ? it->second : CFileSystemObject();
}

// Calculates total size for the specified objects
FilesystemObjectsStatistics CPanel::calculateStatistics(const std::vector<qulonglong>& hashes)
{
	if (hashes.empty())
		return FilesystemObjectsStatistics();

	FilesystemObjectsStatistics stats;
	for(const auto hash: hashes)
	{
		const CFileSystemObject rootItem = itemByHash(hash);
		if (rootItem.isDir())
		{
			++stats.folders;
			scanDirectory(rootItem, [this, &stats](const CFileSystemObject& discoveredItem) {
				if (discoveredItem.isFile())
				{
					stats.occupiedSpace += discoveredItem.size();
					++stats.files;
				}
				else if (discoveredItem.isDir())
					++stats.folders;

				sendItemDiscoveryProgressNotification(0, size_t_max, discoveredItem.fullAbsolutePath());
			});
		}
		else if (rootItem.isFile())
		{
			++stats.files;
			stats.occupiedSpace += rootItem.size();
		}
	}

	return stats;
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::displayDirSize(qulonglong dirHash)
{
	_workerThreadPool.enqueue([this, dirHash] {
		std::unique_lock<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

		auto it = _items.find(dirHash);
		assert_and_return_r(it != _items.end(), );

		if (it->second.isDir())
		{
			locker.unlock(); // Without this .unlock() the UI thread will get blocked very easily
			const FilesystemObjectsStatistics stats = calculateStatistics(std::vector<qulonglong>(1, dirHash));
			locker.lock();
			// Since we unlocked the mutex, the item we were working on may well be out of the _items list by now
			// So we find it again and see if it's still there
			it = _items.find(dirHash);
			if (it == _items.end())
				return;

			it->second.setDirSize(stats.occupiedSpace);
			sendContentsChangedNotification(refreshCauseOther);
		}
	});
}

void CPanel::sendContentsChangedNotification(FileListRefreshCause operation) const
{
	execOnUiThread([this, operation]() {
		_panelContentsChangedListeners.invokeCallback(&PanelContentsChangedListener::panelContentsChanged, _panelPosition, operation);
	}, ContentsChangedNotificationTag);
}

// progress > 100 means indefinite
void CPanel::sendItemDiscoveryProgressNotification(qulonglong itemHash, size_t progress, const QString& currentDir) const
{
	execOnUiThread([this, itemHash, progress, currentDir]() {
		_panelContentsChangedListeners.invokeCallback(&PanelContentsChangedListener::itemDiscoveryInProgress, _panelPosition, itemHash, progress, currentDir);
	}, ItemDiscoveryProgressNotificationTag);
}

void CPanel::volumesChanged(const std::vector<VolumeInfo>& volumes, bool drivesListOrReadinessChanged)
{
	_volumes = volumes;

	if (!drivesListOrReadinessChanged)
		return;

	if (_currentDirObject.isNetworkObject())
		return;

	// Handling an unplugged device
	if (_currentDirObject.isValid() && !volumeInfoForObject(_currentDirObject).isReady)
		setPath(_currentDirObject.fullAbsolutePath(), refreshCauseOther);
}

void CPanel::uiThreadTimerTick()
{
	_uiThreadQueue.exec();
}

void CPanel::contentsChanged()
{
	// The list of items in the current folder is being refreshed asynchronously, not every time a change is detected, to avoid refresh tasks queuing up out of control
	_bContentsChangedEventPending = true;
}

void CPanel::addPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	_panelContentsChangedListeners.addSubscriber(listener);
}

void CPanel::addCurrentItemChangeListener(CursorPositionListener * listener)
{
	_currentItemChangeListener.addSubscriber(listener);
}

const VolumeInfo& CPanel::volumeInfoForObject(const CFileSystemObject& object) const
{
	static const VolumeInfo dummy;

	const auto storage = std::find_if(_volumes.cbegin(), _volumes.cend(), [&object](const VolumeInfo& item) {return item.rootObjectInfo.rootFileSystemId() == object.rootFileSystemId();});
	return storage != _volumes.cend() ? *storage : dummy;
}

//#include <dirent.h>
//bool directoryCanBeOpened(const QString& path)
//{
//	DIR *dir = opendir(path.toUtf8().constData());
//	if (!dir)
//	{
//		const auto err = errno;
//		qInfo() << err << strerror(err);
//		return false;
//	}
//	else
//	{
//		closedir(dir);
//		return true;
//	}
//}

bool CPanel::pathIsAccessible(const QString& path) const
{
	//const CFileSystemObject pathObject(path);
	//if (!pathObject.exists())
	//	return false;

	//const auto storageInfo = volumeInfoForObject(pathObject);
	//if (!pathObject.isNetworkObject() && !storageInfo.isReady)
	//	return false;

	return FileSystemHelpers::pathIsAccessible(path);
}

void CPanel::processContentsChangedEvent()
{
	if (_bContentsChangedEventPending)
	{
		refreshFileList(refreshCauseOther);
		_bContentsChangedEventPending = false;
	}
}
