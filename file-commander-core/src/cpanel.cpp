#include "cpanel.h"
#include "settings/csettings.h"
#include "settings.h"
#include "filesystemhelperfunctions.h"

#include "QtCoreIncludes"

#include <assert.h>
#include <time.h>
#include <limits>

CPanel::CPanel(Panel position) :
	_panelPosition(position)
{
}

void CPanel::restoreFromSettings()
{
	CSettings s;
	const QStringList historyList(s.value(_panelPosition == RightPanel ? KEY_HISTORY_R : KEY_HISTORY_L).toStringList());
	_history.addLatest(historyList.toVector().toStdVector());
	setPath(s.value(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::root().absolutePath()).toString(), refreshCauseOther);
}

FileOperationResultCode CPanel::setPath(const QString &path, FileListRefreshCause operation)
{
#if defined __linux__ || defined __APPLE__
	const QString posixPath(path.contains("~") ? QString(path).replace("~", getenv("HOME")) : path);
#elif defined _WIN32
	const QString posixPath(toPosixSeparators(path));
#else
#error "Not implemented"
#endif

	_currentDisplayMode = NormalMode;

	std::unique_lock<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

	const QString oldPath = _currentDirObject.fullAbsolutePath();
	const auto pathGraph = CFileSystemObject::pathHierarchy(posixPath);
	bool pathSet = false;
	for (const auto& candidatePath: pathGraph)
	{
		if (pathIsAccessible(candidatePath))
		{
			_currentDirObject.setPath(candidatePath);
			pathSet = true;
			break;
		}
	}

	if (!pathSet)
	{
		if (pathIsAccessible(oldPath))
			_currentDirObject.setPath(oldPath);
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
				pathToSet = QDir::rootPath();
			_currentDirObject.setPath(pathToSet);
		}
	}

	const QString newPath = _currentDirObject.fullAbsolutePath();

	// History management
	if (toPosixSeparators(_history.currentItem()) != toPosixSeparators(newPath))
	{
		_history.addLatest(newPath);
		CSettings().setValue(_panelPosition == RightPanel ? KEY_HISTORY_R : KEY_HISTORY_L, QVariant(QStringList::fromVector(QVector<QString>::fromStdVector(_history.list()))));
	}

	CSettings().setValue(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, newPath);

	_watcher = std::make_shared<QFileSystemWatcher>();

	if (_watcher->addPath(newPath) == false)
		qDebug() << __FUNCTION__ << "Error adding path" << newPath << "to QFileSystemWatcher";

	connect(_watcher.get(), &QFileSystemWatcher::directoryChanged, this, &CPanel::contentsChanged);
	connect(_watcher.get(), &QFileSystemWatcher::fileChanged, this, &CPanel::contentsChanged);
	connect(_watcher.get(), &QFileSystemWatcher::objectNameChanged, this, &CPanel::contentsChanged);

	// Finding hash of an item corresponding to path
	for (const auto& item : _items)
	{
		const QString itemPath = toPosixSeparators(item.second.fullAbsolutePath());
		if (posixPath == itemPath && toPosixSeparators(item.second.parentDirPath()) != itemPath)
		{
			setCurrentItemInFolder(item.second.parentDirPath(), item.second.properties().hash);
			break;
		}
	}

	locker.unlock();

	refreshFileList(pathSet ? operation : refreshCauseOther);
	return pathSet ? rcOk : rcDirNotAccessible;
}

// Navigates up the directory tree
void CPanel::navigateUp()
{
	if (_currentDisplayMode != NormalMode)
		setPath(currentDirPathPosix(), refreshCauseOther);
	else
	{
		QDir tmpDir(currentDirPathPosix());
		if (tmpDir.cdUp())
			setPath(tmpDir.absolutePath(), refreshCauseCdUp);
		else
			sendContentsChangedNotification(refreshCauseOther);
	}
}

// Go to the previous location from history
bool CPanel::navigateBack()
{
	if (_currentDisplayMode != NormalMode)
		return setPath(currentDirPathPosix(), refreshCauseOther) == rcOk;
	else if (!_history.empty())
		return setPath(_history.navigateBack(), refreshCauseOther) == rcOk;
	else
		return false;
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (_currentDisplayMode == NormalMode && !_history.empty())
		return setPath(_history.navigateForward(), refreshCauseOther) == rcOk;
	return false;
}

const CHistoryList<QString>& CPanel::history() const
{
	return _history;
}

void CPanel::showAllFilesFromCurrentFolderAndBelow()
{
	_currentDisplayMode = AllObjectsMode;
	_watcher.reset();

	_refreshFileListTask.exec([this]() {
		std::unique_lock<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);
		const QString path = _currentDirObject.fullAbsolutePath();

		locker.unlock();
		auto items = recurseDirectoryItems(path, false);
		locker.lock();

		_items.clear();

		const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		for (const auto& item : items)
		{
			if (item.exists() && (showHiddenFiles || !item.isHidden()))
				_items[item.hash()] = item;
		}

		sendContentsChangedNotification(refreshCauseOther);
	});
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
	return toNativeSeparators(_currentDirObject.fullName());
}

void CPanel::setCurrentItemInFolder(const QString& dir, qulonglong currentItemHash)
{
	_cursorPosForFolder[toPosixSeparators(dir)] = currentItemHash;
}

qulonglong CPanel::currentItemInFolder(const QString &dir) const
{
	const auto it = _cursorPosForFolder.find(toPosixSeparators(dir));
	return it == _cursorPosForFolder.end() ? 0 : it->second;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList(FileListRefreshCause operation)
{
	_refreshFileListTask.exec([this, operation]() {
		const time_t start = clock();
		QFileInfoList list;

		{
			std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

			list = _currentDirObject.qDir().entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
			qDebug() << "Getting file list for" << _currentDirObject.fullAbsolutePath() << "(" << list.size() << "items ) took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

			_items.clear();

			if (list.empty())
			{
				setPath(_currentDirObject.fullAbsolutePath(), operation); // setPath will itself find the closest best folder to set instead
				return;
			}
		}

		const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
		std::vector<CFileSystemObject> objectsList;

		const size_t numItemsFound = list.size();
		objectsList.reserve(numItemsFound);

		for (int i = 0; i < numItemsFound; ++i)
		{
			objectsList.emplace_back(list[i]);
			sendItemDiscoveryProgressNotification(_currentDirObject.hash(), 20 + 80 * i / numItemsFound);
		}

		{
			std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

			for (const auto& object : objectsList)
			{
				if (object.exists() && (showHiddenFiles || !object.isHidden()))
					_items[object.hash()] = object;
			}

			qDebug() << "Directory:" << _currentDirObject.fullAbsolutePath() << "(" << _items.size() << "items ) indexed in" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";
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

	sendItemDiscoveryProgressNotification(0, 0);

	FilesystemObjectsStatistics stats;
	const size_t numItems = hashes.size();
	for(size_t i = 0; i < numItems; ++i)
	{
		CFileSystemObject item = itemByHash(hashes[i]);
		if (item.isDir())
		{
			++stats.folders;
			std::vector <CFileSystemObject> objects = recurseDirectoryItems(item.fullAbsolutePath(), false);
			for (auto& subItem: objects)
			{
				if (subItem.isFile())
					++stats.files;
				else if (subItem.isDir())
					++stats.folders;
				stats.occupiedSpace += subItem.size();
			}
		}
		else if (item.isFile())
		{
			++stats.files;
			stats.occupiedSpace += item.size();
		}

		sendItemDiscoveryProgressNotification(0, i * 100 / numItems);
	}

	sendItemDiscoveryProgressNotification(0, 100);
	return stats;
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::displayDirSize(qulonglong dirHash)
{
	std::lock_guard<std::recursive_mutex> locker(_fileListAndCurrentDirMutex);

	const auto it = _items.find(dirHash);
	if (it == _items.end())
	{
		assert(false);
		return;
	}

	if (it->second.isDir())
	{
		const FilesystemObjectsStatistics stats = calculateStatistics(std::vector<qulonglong>(1, dirHash));
		it->second.setDirSize(stats.occupiedSpace);
		sendContentsChangedNotification(refreshCauseOther);
	}
}

void CPanel::sendContentsChangedNotification(FileListRefreshCause operation) const
{
	_uiThreadQueue.enqueue([this, operation]() {
		for (auto listener : _panelContentsChangedListeners)
			listener->panelContentsChanged(_panelPosition, operation);
	});
}

void CPanel::sendItemDiscoveryProgressNotification(qulonglong itemHash, size_t progress) const
{
	_uiThreadQueue.enqueue([this, itemHash, progress]() {
		for (auto listener : _panelContentsChangedListeners)
			listener->itemDiscoveryInProgress(_panelPosition, itemHash, progress);
	}, 1);
}

void CPanel::disksChanged(const std::vector<CDiskEnumerator::DiskInfo>& disks)
{
	_disks = disks;

	// Handling an unplugged device
	if (_currentDirObject.isValid() && !storageInfoForObject(_currentDirObject).isReady())
		setPath(_currentDirObject.fullAbsolutePath(), refreshCauseOther);
}

// Settings have changed
void CPanel::settingsChanged()
{
}

void CPanel::uiThreadTimerTick()
{
	_uiThreadQueue.exec();
}

void CPanel::contentsChanged(QString /*path*/)
{
	refreshFileList(refreshCauseOther);
}

void CPanel::addPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	assert(std::find(_panelContentsChangedListeners.begin(), _panelContentsChangedListeners.end(), listener) == _panelContentsChangedListeners.end()); // Why would we want to set the same listener twice? That'd probably be a mistake.
	_panelContentsChangedListeners.push_back(listener);
}

const QStorageInfo& CPanel::storageInfoForObject(const CFileSystemObject& object) const
{
	static const QStorageInfo dummy;

	const auto storage = std::find_if(_disks.cbegin(), _disks.cend(), [&object](const CDiskEnumerator::DiskInfo& item) {return item.fileSystemObject.rootFileSystemId() == object.rootFileSystemId();});
	return storage != _disks.cend() ? storage->storageInfo : dummy;
}

bool CPanel::pathIsAccessible(const QString& path) const
{
	const CFileSystemObject pathObject(path);
	return pathObject.exists() && storageInfoForObject(pathObject).isReady() && pathObject.isReadable() && !pathObject.qDir().entryList().isEmpty();
}
