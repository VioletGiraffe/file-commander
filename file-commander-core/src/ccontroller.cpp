#include "ccontroller.h"
#include "settings/csettings.h"
#include "settings.h"
#include "pluginengine/cpluginengine.h"
#include "shell/cshell.h"
#include "filesystemhelperfunctions.h"
#include "iconprovider/ciconprovider.h"

#include <stdlib.h>
#include <assert.h>

CController* CController::_instance = nullptr;

CController::CController() : _leftPanel(LeftPanel), _rightPanel(RightPanel), _diskEnumerator(CDiskEnumerator::instance())
{
	assert(_instance == nullptr); // Only makes sense to create one controller
	_instance = this;

	_diskEnumerator.addObserver(this);
	CPluginEngine::get().loadPlugins();

	_leftPanel.addPanelContentsChangedListener(&CPluginEngine::get());
	_rightPanel.addPanelContentsChangedListener(&CPluginEngine::get());
}

CController& CController::get()
{
	assert(_instance);
	return *_instance;
}

void CController::setPanelContentsChangedListener(Panel p, PanelContentsChangedListener *listener)
{
	panel(p).addPanelContentsChangedListener(listener);
}

void CController::setDisksChangedListener(CController::IDiskListObserver *listener)
{
	assert(std::find(_disksChangedListeners.begin(), _disksChangedListeners.end(), listener) == _disksChangedListeners.end());
	_disksChangedListeners.push_back(listener);
}

// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
void CController::refreshPanelContents(Panel p)
{
	panel(p).refreshFileList(refreshCauseOther);
}

// Creates a new tab for the specified panel, returns tab ID
int CController::tabCreated(Panel /*p*/)
{
	return -1;
}

// Removes a tab for the specified panel and tab ID
void CController::tabRemoved(Panel /*panel*/, int /*tabId*/)
{

}

// Indicates that an item was activated and appropriate action should be taken. Returns error message, if any
FileOperationResultCode CController::itemActivated(qulonglong itemHash, Panel p)
{
	auto& item = panel(p).itemByHash(itemHash);
	if (item.isDir())
	{
		// Attempting to enter this dir
		const FileOperationResultCode result = setPath(p, item.absoluteFilePath(), item.isCdUp() ? refreshCauseCdUp : refreshCauseForwardNavigation);
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
void CController::diskSelected(Panel p, int index)
{
	assert(index < _diskEnumerator.drives().size());
	const QString drivePath = _diskEnumerator.drives().at(index).rootPath();

	if (drivePath == _diskEnumerator.drives().at(currentDiskIndex(otherPanelPosition(p))).rootPath())
	{
		setPath(p, otherPanel(p).currentDirPath(), refreshCauseOther);
	}
	else
	{
		const QString lastPathForDrive = CSettings().value(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), drivePath).toString();
		setPath(p, lastPathForDrive, refreshCauseOther);
	}
}

// Porgram settings have changed
void CController::settingsChanged()
{
	_rightPanel.settingsChanged();
	_leftPanel.settingsChanged();

	CIconProvider::settingsChanged();
}

void CController::activePanelChanged(Panel p)
{
	_activePanel = p;
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
FileOperationResultCode CController::setPath(Panel p, const QString &path, FileListRefreshCause operation)
{
	CPanel& targetPanel = panel(p);
	const QString prevPath = targetPanel.currentDirPath();
	const FileOperationResultCode result = targetPanel.setPath(path, operation);
	if (result != rcOk) // Restoring the previous position
		targetPanel.setPath(prevPath, refreshCauseOther);

	saveDirectoryForCurrentDisk(p);
	disksChanged(); // To select a proper drive button
	return result;
}

bool CController::createFolder(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return false;

	const QString posixName = toPosixSeparators(name);
	if (parentDir.mkpath(posixName))
	{
		if (toNativeSeparators(parentDir.absolutePath()) == activePanel().currentDirPath())
		{
			const int slashPosition = posixName.indexOf('/');
			const QString newFolderPath = parentDir.absolutePath() + "/" + (slashPosition > 0 ? posixName.left(posixName.indexOf('/')) : posixName);
			// This is required for the UI to know to set the cursor at the new folder
			activePanel().setCurrentItemInFolder(activePanel().currentDirPath(), CFileSystemObject(newFolderPath).hash());
			activePanel().refreshFileList(refreshCauseNewItemCreated);
		}

		return true;
	}
	else
		return false;
}

bool CController::createFile(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return false;

	const QString newFilePath = parentDir.absolutePath() + "/" + name;
	if (QFile(newFilePath).open(QFile::WriteOnly))
	{
		if (toPosixSeparators(parentDir.absolutePath()) == activePanel().currentDirPath())
		{
			// This is required for the UI to know to set the cursor at the new file
			activePanel().setCurrentItemInFolder(activePanel().currentDirPath(), CFileSystemObject(newFilePath).hash());
			activePanel().refreshFileList(refreshCauseNewItemCreated);
		}

		return true;
	}
	else
		return false;
}

void CController::openTerminal(const QString &folder)
{
#ifdef _WIN32
	const bool started = QProcess::startDetached(CShell::shellExecutable(), QStringList(), folder);
	assert(started);
	Q_UNUSED(started);
#elif defined __APPLE__
	system(QString("osascript -e \"tell application \\\"Terminal\\\" to do script \\\"cd %1\\\"\"").arg(folder).toUtf8().data());
#elif defined __linux__
	const bool started = QProcess::startDetached(CShell::shellExecutable(), QStringList(), folder);
	assert(started);
	Q_UNUSED(started);
#else
	#error unknown platform
#endif
}

FilesystemObjectsStatistics CController::calculateStatistics(Panel p, const std::vector<qulonglong> & hashes)
{
	return panel(p).calculateStatistics(hashes);
}

// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
void CController::displayDirSize(Panel p, qulonglong dirHash)
{
	panel(p).displayDirSize(dirHash);
}

void CController::showAllFilesFromCurrentFolderAndBelow(Panel p)
{
	panel(p).showAllFilesFromCurrentFolderAndBelow();
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

CPanel& CController::panel(Panel p)
{
	switch (p)
	{
	case LeftPanel:
		return _leftPanel;
	case RightPanel:
		return _rightPanel;
	default:
		assert(false);
		return _rightPanel;
	}
}

const CPanel &CController::otherPanel(Panel p) const
{
	switch (p)
	{
	case LeftPanel:
		return _rightPanel;
	case RightPanel:
		return _leftPanel;
	default:
		assert(false);
		return _rightPanel;
	}
}

CPanel& CController::otherPanel(Panel p)
{
	switch (p)
	{
	case LeftPanel:
		return _rightPanel;
	case RightPanel:
		return _leftPanel;
	default:
		assert(false);
		return _leftPanel;
	}
}


Panel CController::otherPanelPosition(Panel p)
{
	switch (p)
	{
	case LeftPanel:
		return RightPanel;
	case RightPanel:
		return LeftPanel;
	default:
		assert(false);
		return LeftPanel;
	}
}


Panel CController::activePanelPosition() const
{
	return _activePanel;
}

const CPanel& CController::activePanel() const
{
	return panel(activePanelPosition());
}

CPanel& CController::activePanel()
{
	return panel(activePanelPosition());
}

CPluginProxy &CController::pluginProxy()
{
	return _pluginProxy;
}

bool CController::itemHashExists(Panel p, qulonglong hash) const
{
	return panel(p).itemHashExists(hash);
}

const CFileSystemObject& CController::itemByIndex( Panel p, size_t index ) const
{
	return panel(p).itemByIndex(index);
}

const CFileSystemObject& CController::itemByHash( Panel p, qulonglong hash ) const
{
	return panel(p).itemByHash(hash);
}

CFileSystemObject &CController::itemByHash(Panel p, qulonglong hash)
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

QString CController::diskPath(int index) const
{
	return index < _diskEnumerator.drives().size() && index >= 0 ? _diskEnumerator.drives().at(index).rootPath() : QString();
}

CFavoriteLocations &CController::favoriteLocations()
{
	return _favoriteLocations;
}

// Returns hash of an item that was the last selected in the specified dir
qulonglong CController::currentItemInFolder(Panel p, const QString &dir) const
{
	return panel(p).currentItemInFolder(dir);
}

void CController::disksChanged()
{

	const int leftPanelRoot  = currentDiskIndex(LeftPanel);
	const int rightPanelRoot = currentDiskIndex(RightPanel);
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
	{
		assert(false);
		return;
	}

	const QString drivePath = _diskEnumerator.drives().at(currentDiskIndex(p)).rootPath();
	const QString path = panel(p).currentDirPath();
	CSettings().setValue(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), path);
}


int CController::currentDiskIndex(Panel p) const
{
	const auto& drives = _diskEnumerator.drives();
	for (int i = 0; i < drives.size(); ++i)
	{
		if (CFileSystemObject(panel(p).currentDirPath()).isChildOf(CFileSystemObject(QFileInfo(drives[i].rootPath()))))
			return i;
	}

	return std::numeric_limits<int>::max();
}
