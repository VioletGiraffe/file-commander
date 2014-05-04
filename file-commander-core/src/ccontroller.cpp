#include "ccontroller.h"
#include "settings/csettings.h"
#include "settings.h"

#include <stdlib.h>
#include <assert.h>

CController::CController() : _leftPanel(LeftPanel), _rightPanel(RightPanel), _diskEnumerator(CDiskEnumerator::instance())
{
	_diskEnumerator.addObserver(this);
	_pluginEngine.loadPlugins();

	_leftPanel.addPanelContentsChangedListener(&_pluginEngine);
	_rightPanel.addPanelContentsChangedListener(&_pluginEngine);
}

CController& CController::get()
{
	static CController cnt;
	return cnt;
}

CPluginEngine& CController::pluginEngine()
{
	return _pluginEngine;
}

void CController::setPanelContentsChangedListener(PanelContentsChangedListener *listener)
{
	_leftPanel.addPanelContentsChangedListener(listener);
	_rightPanel.addPanelContentsChangedListener(listener);
}

void CController::setDisksChangedListener(CController::IDiskListObserver *listener)
{
	assert(std::find(_disksChangedListeners.begin(), _disksChangedListeners.end(), listener) == _disksChangedListeners.end());
	_disksChangedListeners.push_back(listener);
}

// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
void CController::refreshPanelContents (Panel p)
{
	panel(p).refreshFileList();
}

// Creates a new tab for the specified panel, returns tab ID
int CController::tabCreated (Panel /*p*/)
{
	return -1;
}

// Removes a tab for the specified panel and tab ID
void CController::tabRemoved (Panel /*panel*/, int /*tabId*/)
{

}

// Indicates that an item was activated and appropriate action should be taken. Returns error message, if any
FileOperationResultCode CController::itemActivated(qulonglong itemHash, Panel p)
{
	auto& item = panel(p).itemByHash(itemHash);
	if (item.isDir())
	{
		// Attempting to enter this dir
		const FileOperationResultCode result = setPath(p, item.absoluteFilePath());
		return result;
	}
	else if (item.isFile())
	{
		if (item.isExecutable())
			// Attempting to launch this exe from the current directory
			return QProcess::startDetached(toPosixSeparators(item.absoluteFilePath()), QStringList(), toPosixSeparators(item.parentDirPath())) ? rcOk : rcFail;
		else
			// It's probably not a binary file, try opening with openUrl
			return QDesktopServices::openUrl(QUrl::fromLocalFile(toPosixSeparators(item.absoluteFilePath()))) ? rcOk : rcFail;
	}

	return rcFail;
}

// A current disk has been switched
void CController::diskSelected(Panel p, size_t index)
{
	assert(index < _diskEnumerator.drives().size());
	const QString drivePath = _diskEnumerator.drives()[index].fileSystemObject.absoluteFilePath();
	const QString lastPathForDrive = CSettings().value(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), drivePath).toString();
	setPath(p, lastPathForDrive);
}

// Porgram settings have changed
void CController::settingsChanged()
{
	_rightPanel.settingsChanged();
	_leftPanel.settingsChanged();
}

// Navigates specified panel up the directory tree
void CController::navigateUp(Panel p)
{
	panel(p).navigateUp();
	disksChanged(); // To select a proper drive button
}

// Go to the previous location from history, if any
void CController::navigateBack(Panel p)
{
	panel(p).navigateBack();
	saveDirectoryForCurrentDisk(p);
	disksChanged(); // To select a proper drive button
}

// Go to the next location from history, if any
void CController::navigateForward(Panel p)
{
	panel(p).navigateForward();
	saveDirectoryForCurrentDisk(p);
	disksChanged(); // To select a proper drive button
}

// Sets the specified path, if possible. Otherwise reverts to the previously set path
FileOperationResultCode CController::setPath(Panel p, const QString &path)
{
	CPanel& targetPanel = panel(p);
	const QString prevPath = targetPanel.currentDirPath();
	const FileOperationResultCode result = targetPanel.setPath(path);
	if (result != rcOk)
		targetPanel.setPath(prevPath);

	saveDirectoryForCurrentDisk(p);
	disksChanged(); // To select a proper drive button
	return result;
}

bool CController::createFolder(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return false;
	return parentDir.mkpath(name);
}

bool CController::createFile(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return false;
	return QFile(parentDir.absolutePath() + "/" + name).open(QFile::WriteOnly);
}

void CController::openTerminal(const QString &folder)
{
#ifdef _WIN32
	const bool started = QProcess::startDetached(shellExecutable(), QStringList(), folder);
	assert(started);
	Q_UNUSED(started);
#elif defined __APPLE__
	const bool started = QProcess::startDetached(shellExecutable(), QStringList() << folder);
	assert(started);
	Q_UNUSED(started);
#elif defined __linux__
	const bool started = QProcess::startDetached(shellExecutable(), QStringList(), folder);
	assert(started);
	Q_UNUSED(started);
#else
	#error unknown platform
#endif
}

// Rename a file or folder
void CController::rename(Panel p, size_t itemIdx, const QString &newName)
{
	panel(p).itemByIndex(itemIdx).rename(newName);
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CController::calculateDirSize(Panel p, size_t dirIndex)
{
	panel(p).calculateDirSize(dirIndex);
}

const CPanel &CController::panel(Panel p) const
{
	switch (p)
	{
	case LeftPanel:
		return _leftPanel;
	case RightPanel:
		return _rightPanel;
	default:
		assert (false);
		return _rightPanel;
	}
}

CPanel &CController::panel(Panel p)
{
	switch (p)
	{
	case LeftPanel:
		return _leftPanel;
	case RightPanel:
		return _rightPanel;
	default:
		assert (false);
		return _rightPanel;
	}
}

const CFileSystemObject& CController::itemByIndex( Panel p, size_t index ) const
{
	return panel(p).itemByIndex(index);
}

const CFileSystemObject& CController::itemByHash( Panel p, qulonglong hash ) const
{
	return panel(p).itemByHash(hash);
}

std::vector<CFileSystemObject> CController::items(Panel p, const std::vector<qulonglong>& hashes) const
{
	std::vector<CFileSystemObject> objects;
	std::for_each(hashes.begin(), hashes.end(), [&objects, p, this] (qulonglong hash) {objects.push_back(itemByHash(p, hash));});
	return objects;
}

size_t CController::numItems(Panel p) const
{
	return panel(p).list().size();
}

QString CController::itemPath(Panel p, qulonglong hash) const
{
	return panel(p).itemByHash(hash).properties().fullPath;
}

QString CController::diskPath(size_t index) const
{
	return index < _diskEnumerator.drives().size() ? _diskEnumerator.drives().at(index).fileSystemObject.absoluteFilePath() : QString();
}

// Returns hash of an item that was the last selected in the specified dir
qulonglong CController::currentItemInFolder(Panel p, const QString &dir) const
{
	return panel(p).currentItemInFolder(dir);
}

QString CController::shellExecutable()
{
#ifdef _WIN32
	static const QString defaultShell = QProcessEnvironment::systemEnvironment().value("ComSpec", "cmd.exe");
	return CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME, defaultShell).toString();
#elif defined __APPLE__
	return CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME, "/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal").toString();
#elif defined __linux__
	QString consoleExecutable = "/usr/bin/konsole"; // KDE
	if (!QFileInfo(consoleExecutable).exists())
		consoleExecutable = "/usr/bin/gnome-terminal"; // Gnome
	if (!QFileInfo(consoleExecutable).exists())
		consoleExecutable = QString();
	return CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME, consoleExecutable).toString();
#else
	#error unknown platform
#endif
}

void CController::disksChanged()
{

	const size_t leftPanelRoot  = currentDiskIndex(LeftPanel);
	const size_t rightPanelRoot = currentDiskIndex(RightPanel);
	const auto& drives = _diskEnumerator.drives();

	for (auto& listener: _disksChangedListeners)
	{
		listener->disksChanged(drives, RightPanel, rightPanelRoot);
		listener->disksChanged(drives, LeftPanel, leftPanelRoot);
	}
}

void CController::saveDirectoryForCurrentDisk(Panel p)
{
	if(currentDiskIndex(p) >= _diskEnumerator.drives().size())
		return;

	const QString drivePath = _diskEnumerator.drives()[currentDiskIndex(p)].fileSystemObject.absoluteFilePath();
	const QString path = panel(p).currentDirPath();
	CSettings().setValue(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), path);
}


size_t CController::currentDiskIndex(Panel p) const
{
	const auto& drives = _diskEnumerator.drives();
	for (size_t i = 0; i < drives.size(); ++i)
	{
		if (CFileSystemObject(panel(p).currentDirPath()).isChildOf(drives[i].fileSystemObject))
			return i;
	}

	return std::numeric_limits<size_t>::max();
}
