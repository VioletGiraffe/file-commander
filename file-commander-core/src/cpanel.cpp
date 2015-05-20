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

	const QString oldPath = _currentDir.absolutePath();
	auto pathGraph = CFileSystemObject(posixPath).pathHierarchy();
	bool pathSet = false;
	for (const auto& candidatePath: pathGraph)
	{
		_currentDir.setPath(candidatePath);
		pathSet = _currentDir.exists() && ! _currentDir.entryList().isEmpty(); // No dot and dotdot on Linux means the dir is not accessible
		if (pathSet)
			break;
	}
	
	if (!pathSet)
	{
		if (CFileSystemObject(oldPath).exists())
			_currentDir.setPath(oldPath);
		else
		{
			QString pathToSet;
			for (auto it = history().rbegin() + (history().size() - 1 - history().currentIndex()); it != history().rend(); ++it)
			{
				if (CFileSystemObject(*it).exists())
				{
					pathToSet = *it;
					break;
				}
			}

			if (pathToSet.isEmpty())
				pathToSet = QDir::rootPath();
			_currentDir.setPath(pathToSet);
		}
	}

	const QString newPath = _currentDir.absolutePath();
	// History management
	if (toPosixSeparators(_history.currentItem()) != toPosixSeparators(newPath))
	{
		_history.addLatest(newPath);
		CSettings().setValue(_panelPosition == RightPanel ? KEY_HISTORY_R : KEY_HISTORY_L, QVariant(QStringList::fromVector(QVector<QString>::fromStdVector(_history.list()))));
	}

	CSettings().setValue(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, currentDirPath());

	_watcher = std::make_shared<QFileSystemWatcher>();

#if QT_VERSION >= QT_VERSION_CHECK (5,0,0)
	const QString watchPath(newPath);
#else
	const QString watchPath(posixPath);
#endif
	if (_watcher->addPath(watchPath) == false)
		qDebug() << __FUNCTION__ << "Error adding path" << watchPath << "to QFileSystemWatcher";

	connect (_watcher.get(), SIGNAL(directoryChanged(QString)), SLOT(contentsChanged(QString)));
	connect (_watcher.get(), SIGNAL(fileChanged(QString)), SLOT(contentsChanged(QString)));
#if QT_VERSION >= QT_VERSION_CHECK (5,0,0)
	connect (_watcher.get(), SIGNAL(objectNameChanged(QString)), SLOT(contentsChanged(QString)));
#endif

	// Finding hash of an item corresponding to path
	for (const CFileSystemObject& item: _list)
	{
		const QString itemPath = toPosixSeparators(item.fullAbsolutePath());
		if (posixPath == itemPath && toPosixSeparators(item.parentDirPath()) != itemPath)
		{
			setCurrentItemInFolder(item.parentDirPath(), item.properties().hash);
			break;
		}
	}

	refreshFileList(pathSet ? operation : refreshCauseOther);
	return pathSet ? rcOk : rcDirNotAccessible;
}

// Navigates up the directory tree
void CPanel::navigateUp()
{
	if (_currentDisplayMode != NormalMode)
		setPath(_currentDir.absolutePath(), refreshCauseOther);
	else
	{
		QDir tmpDir(_currentDir);
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
		return setPath(_currentDir.absolutePath(), refreshCauseOther) == rcOk;
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
	auto items = recurseDirectoryItems(_currentDir.absolutePath(), false);

	_list.clear();
	_indexByHash.clear();
	const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
	for (const auto& item: items)
	{
		if (item.exists() && (showHiddenFiles || !item.isHidden()))
		{
			_list.push_back(item);
			_indexByHash[_list.back().hash()] = _list.size() - 1;
		}
	}

	sendContentsChangedNotification(refreshCauseOther);
}

// Info on the dir this panel is currently set to
QString CPanel::currentDirPath() const
{
	return toNativeSeparators(_currentDir.absolutePath());
}

QString CPanel::currentDirName() const
{
	return toNativeSeparators(_currentDir.dirName());
}

void CPanel::setCurrentItemInFolder(const QString& dir, qulonglong currentItemHash)
{
	_cursorPosForFolder[toPosixSeparators(dir)] = currentItemHash;
}

qulonglong CPanel::currentItemInFolder(const QString &dir) const
{
	const auto it = _cursorPosForFolder.find(toPosixSeparators(dir));
	if (it == _cursorPosForFolder.end())
		return 0;
	else
		return it->second;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList(FileListRefreshCause operation)
{
	const time_t start = clock();
	const QFileInfoList list(_currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System));
	qDebug() << "Getting file list for" << _currentDir.absolutePath() << "(" << list.size() << "items) took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";
	_list.clear();
	_indexByHash.clear();

	if (list.empty())
	{
		setPath(_currentDir.absolutePath(), operation); // setPath will itself find the closest best folder to set instead
		return;
	}

	const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
	for (const auto& item: list)
	{
		_list.emplace_back(item);
		if (!_list.back().exists() || (!showHiddenFiles && _list.back().isHidden()))
			_list.pop_back();
		else
			_indexByHash[_list.back().hash()] = _list.size() - 1;
	}

	qDebug () << __FUNCTION__ << "Directory:" << _currentDir.absolutePath() << QString("(%1 items) indexed in").arg(_list.size()) << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";
	sendContentsChangedNotification(operation);
}

// Returns the current list of objects on this panel
const std::vector<CFileSystemObject> &CPanel::list() const
{
	return _list;
}

// Access to the corresponding item
const CFileSystemObject &CPanel::itemByIndex(size_t index) const
{
	if (index < _list.size())
	{
		return _list[index];
	}
	else
	{
		assert (false);
		static CFileSystemObject dummyObject((QFileInfo()));
		return dummyObject;
	}
}

CFileSystemObject &CPanel::itemByIndex(size_t index)
{
	if (index < _list.size())
		return _list[index];
	else
	{
		assert (false);
		static CFileSystemObject dummyObject ((QFileInfo()));
		return dummyObject;
	}
}

bool CPanel::itemHashExists(const qulonglong hash) const
{
	return _indexByHash.count(hash) > 0;
}

const CFileSystemObject& CPanel::itemByHash( qulonglong hash ) const
{
	assert(itemHashExists(hash));
	return itemByIndex(_indexByHash.at(hash));
}

CFileSystemObject& CPanel::itemByHash( qulonglong hash )
{
	if (_indexByHash.count(hash) == 0)
	{
		static CFileSystemObject dummy;
		dummy = CFileSystemObject();
		return dummy;
	}
	return itemByIndex(_indexByHash[hash]);
}

// Calculates total size for the specified objects
FilesystemObjectsStatistics CPanel::calculateStatistics(const std::vector<qulonglong>& hashes)
{
	if (hashes.empty())
		return FilesystemObjectsStatistics();

	FilesystemObjectsStatistics stats;
	for(qulonglong itemHash: hashes)
	{
		CFileSystemObject& item = itemByHash(itemHash);
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
	}
	return stats;
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::displayDirSize(qulonglong dirHash)
{
	assert(dirHash != 0);
	CFileSystemObject& item = itemByHash(dirHash);
	if (item.isDir())
	{
		const FilesystemObjectsStatistics stats = calculateStatistics(std::vector<qulonglong>(1, dirHash));
		item.setDirSize(stats.occupiedSpace);
		sendContentsChangedNotification(refreshCauseOther);
	}
}

void CPanel::sendContentsChangedNotification(FileListRefreshCause operation) const
{
	for (auto listener: _panelContentsChangedListeners)
		listener->panelContentsChanged(_panelPosition, operation);
}

// Settings have changed
void CPanel::settingsChanged()
{

}

void CPanel::contentsChanged(QString /*path*/)
{
	refreshFileList(refreshCauseOther);
}

void CPanel::addPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	assert(std::find(_panelContentsChangedListeners.begin(), _panelContentsChangedListeners.end(), listener) == _panelContentsChangedListeners.end()); // Why would we want to set the same listener twice? That'd probably be a mistake.
	_panelContentsChangedListeners.push_back(listener);
	sendContentsChangedNotification(refreshCauseOther);
}
