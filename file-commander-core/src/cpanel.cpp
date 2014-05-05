#include "cpanel.h"
#include "settings/csettings.h"
#include "settings.h"

#include "QtCoreIncludes"

#include <assert.h>
#include <time.h>
#include <limits>

const size_t CPanel::noHistory = std::numeric_limits<size_t>::max();

CPanel::CPanel(Panel position) :
	_currentHistoryLocation(noHistory),
	_panelPosition(position)
{
	setPath(CSettings().value(_panelPosition == LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::root().absolutePath()).toString());
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
	if (_currentHistoryLocation == noHistory || _history.empty())
	{
		assert (_history.empty() && _currentHistoryLocation == noHistory);
		_history.push_back(newPath);
		_currentHistoryLocation = 0;
	}
	else
	{
		assert (_currentHistoryLocation < _history.size());
		if (toPosixSeparators(_history[_currentHistoryLocation]) != toPosixSeparators(newPath))
		{
			_history.push_back(newPath);
			_currentHistoryLocation = _history.size() - 1;
		}
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
			_cursorPosForFolder[item.parentDirPath()] = item.properties().hash;
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
	if (_currentHistoryLocation > 0 && _currentHistoryLocation < _history.size())
		return setPath(_history[--_currentHistoryLocation]) == rcOk;
	else
		sendContentsChangedNotification();
	return false;
}

// Go to the next location from history, if any
bool CPanel::navigateForward()
{
	if (_currentHistoryLocation != noHistory && _currentHistoryLocation < _history.size() - 1)
		return setPath(_history[++_currentHistoryLocation]) == rcOk;
	else
		sendContentsChangedNotification();
	return false;
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

qulonglong CPanel::currentItemInFolder(const QString &dir) const
{
	if (_cursorPosForFolder.count(dir) > 0)
		return _cursorPosForFolder.at(dir);
	else
		return 0;
}

// Enumerates objects in the current directory
void CPanel::refreshFileList()
{
	time_t start = clock();
	QFileInfoList list = _currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System);
	_list.clear();
	_indexByHash.clear();
	for (int i = 0; i < list.size(); ++i)
	{
		if (list[i].absoluteFilePath() != "/..")
		{
			_list.push_back(CFileSystemObject(list[i]));
			if (!_list.back().exists())
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

const CFileSystemObject& CPanel::itemByHash( qulonglong hash ) const
{
	assert(_indexByHash.count(hash) > 0);
	return itemByIndex(_indexByHash.at(hash));
}

CFileSystemObject& CPanel::itemByHash( qulonglong hash )
{
	assert(_indexByHash.count(hash) > 0);
	return itemByIndex(_indexByHash[hash]);
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CPanel::calculateDirSize(size_t dirIndex)
{
	assert(dirIndex < _list.size());
	CFileSystemObject& item = _list[dirIndex];
	if (item.isDir())
	{
		uint64_t size = 0;
		std::vector <CFileSystemObject> objects = recurseDirectoryItems(item.absoluteFilePath(), false);
		for (auto& dirItem: objects)
			size += dirItem.size();
		item.setDirSize(size);
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
