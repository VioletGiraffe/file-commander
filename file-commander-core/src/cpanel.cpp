#include "cpanel.h"
#include "settings/csettings.h"
#include "settings.h"

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
	setPath(s.value(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::root().absolutePath()).toString());
}

FileOperationResultCode CPanel::setPath(const QString &path)
{
#if defined __linux__ || defined __APPLE__
	const QString posixPath(path.contains("~") ? QString(path).replace("~", getenv("HOME")) : path);
#elif defined _WIN32
	const QString posixPath(toPosixSeparators(path));
#else
#error "Not implemented"
#endif

	const QString oldPath = _currentDir.absolutePath();
	_currentDir.setPath(posixPath);
	const QString newPath = _currentDir.absolutePath();
	if (!_currentDir.exists() || _currentDir.entryList().isEmpty()) // No dot and dotdot on Linux means the dir is not accessible
	{
		_currentDir.setPath(oldPath);
		sendContentsChangedNotification();
		return rcDirNotAccessible;
	}

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
		const QString itemPath = toPosixSeparators(item.absoluteFilePath());
		if (posixPath == itemPath && toPosixSeparators(item.parentDirPath()) != itemPath)
		{
			setCurrentItemInFolder(item.parentDirPath(), item.properties().hash);
			break;
		}
	}

	refreshFileList();
	return rcOk;
}

// Navigates up the directory tree
void CPanel::navigateUp()
{
	QDir tmpDir (_currentDir);
	if (tmpDir.cdUp())
		setPath(tmpDir.absolutePath());
	else
		sendContentsChangedNotification();
}

// Go to the previous location from history
bool CPanel::navigateBack()
{
	if (!_history.empty())
		return setPath(_history.navigateBack()) == rcOk;
	return false;
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (!_history.empty())
		return setPath(_history.navigateForward()) == rcOk;
	return false;
}

const CHistoryList<QString>& CPanel::history() const
{
	return _history;
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
void CPanel::refreshFileList()
{
	time_t start = clock();
	QFileInfoList list = _currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
	_list.clear();
	_indexByHash.clear();
	const bool showHiddenFiles = CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool();
	for (int i = 0; i < list.size(); ++i)
	{
		if (list[i].absoluteFilePath() != "/..")
		{
			_list.push_back(CFileSystemObject(list[i]));
			if (!_list.back().exists() || (!showHiddenFiles && _list.back().isHidden()))
				_list.pop_back();
			else
				_indexByHash[_list.back().hash()] = _list.size() - 1;
		}
	}

	qDebug () << __FUNCTION__ << "Directory:" << _currentDir.absolutePath() << "," << _list.size() << "items," << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";
	sendContentsChangedNotification();
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
	assert(_indexByHash.count(hash) > 0);
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
			std::vector <CFileSystemObject> objects = recurseDirectoryItems(item.absoluteFilePath(), false);
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
		sendContentsChangedNotification();
	}
}

void CPanel::sendContentsChangedNotification() const
{
	for (auto listener: _panelContentsChangedListeners)
		listener->panelContentsChanged(_panelPosition);
}

// Settings have changed
void CPanel::settingsChanged()
{

}

void CPanel::contentsChanged(QString /*path*/)
{
	refreshFileList();
}

void CPanel::addPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	assert(std::find(_panelContentsChangedListeners.begin(), _panelContentsChangedListeners.end(), listener) == _panelContentsChangedListeners.end()); // Why would we want to set the same listener twice? That'd probably be a mistake.
	_panelContentsChangedListeners.push_back(listener);
	sendContentsChangedNotification();
}
