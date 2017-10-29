#include "ccontroller.h"
#include "settings/csettings.h"
#include "settings.h"
#include "shell/cshell.h"
#include "pluginengine/cpluginengine.h"
#include "filesystemhelperfunctions.h"
#include "iconprovider/ciconprovider.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <stdlib.h>

CController* CController::_instance = nullptr;

CController::CController() :
	_fileSearchEngine(*this),
	_leftPanel(LeftPanel),
	_rightPanel(RightPanel),
	_workerThread(32, "CController worker thread")
{
	assert_r(_instance == nullptr); // Only makes sense to create one controller
	_instance = this;

	_diskEnumerator.addObserver(this);

	_leftPanel.addPanelContentsChangedListener(&CPluginEngine::get());
	_rightPanel.addPanelContentsChangedListener(&CPluginEngine::get());

	// Manual update for the CPanels to get the disk list
	_diskEnumerator.updateSynchronously();

	_leftPanel.restoreFromSettings();
	_rightPanel.restoreFromSettings();
}

CController& CController::get()
{
	assert_r(_instance);
	return *_instance;
}

void CController::loadPlugins()
{
	CPluginEngine::get().loadPlugins();
}

void CController::setPanelContentsChangedListener(Panel p, PanelContentsChangedListener *listener)
{
	panel(p).addPanelContentsChangedListener(listener);
}

void CController::setDisksChangedListener(CController::IDiskListObserver *listener)
{
	assert_r(std::find(_disksChangedListeners.begin(), _disksChangedListeners.end(), listener) == _disksChangedListeners.end());
	_disksChangedListeners.push_back(listener);

	// Force an update
	disksChanged();
}

void CController::uiThreadTimerTick()
{
	_leftPanel.uiThreadTimerTick();
	_rightPanel.uiThreadTimerTick();

	_uiQueue.exec(CExecutionQueue::execAll);
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
	const auto item = panel(p).itemByHash(itemHash);
	if (item.isDir())
	{
		// Attempting to enter this dir
		const FileOperationResultCode result = setPath(p, item.fullAbsolutePath(), item.isCdUp() ? refreshCauseCdUp : refreshCauseForwardNavigation);
		return result;
	}
	else if (item.isFile())
	{
		if (item.isExecutable())
			// Attempting to launch this exe from the current directory
			return CShell::runExecutable(item.fullAbsolutePath(), QString(), item.parentDirPath()) ? rcOk : rcFail;
		else
			// It's probably not a binary file, try opening with openUrl
			return QDesktopServices::openUrl(QUrl::fromLocalFile(item.fullAbsolutePath())) ? rcOk : rcFail;
	}

	return rcFail;
}

// A current disk has been switched
bool CController::switchToDisk(Panel p, size_t index)
{
	assert_r(index < _diskEnumerator.drives().size());
	const QString drivePath = _diskEnumerator.drives().at(index).storageInfo.rootPath();

	const size_t currentIndex = currentDiskIndex(otherPanelPosition(p));
	if (currentIndex < _diskEnumerator.drives().size() && drivePath == _diskEnumerator.drives().at(currentIndex).storageInfo.rootPath())
	{
		return setPath(p, otherPanel(p).currentDirPathPosix(), refreshCauseOther) == rcOk;
	}
	else
	{
		const QString lastPathForDrive = CSettings().value(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), drivePath).toString();
		return setPath(p, toPosixSeparators(lastPathForDrive), refreshCauseOther) == rcOk;
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
	saveDirectoryForCurrentDisk(p);
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
	const FileOperationResultCode result = panel(p).setPath(path, operation);

	saveDirectoryForCurrentDisk(p);
	disksChanged(); // To select a proper drive button
	return result;
}

bool CController::createFolder(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return false;

	const auto currentItemHash = currentItemHashForFolder(_activePanel, parentDir.absolutePath());

	if (parentDir.absolutePath() == activePanel().currentDirObject().qDir().absolutePath())
	{
		const int slashPosition = name.indexOf('/');
		// The trailing slash is required in order for the hash to match the hash of the item once it will be created: existing folders always have a trailing hash
		const QString newFolderPath = parentDir.absolutePath() % '/' % (slashPosition > 0 ? name.left(slashPosition) : name) % '/';
		// This is required for the UI to know to set the cursor at the new folder.
		// It must be done before calling mkpath, or #133 will occur due to asynchronous file list refresh between mkpath and the current item selection logic (it gets overwritten from CPanelWidget::fillFromList).
		qDebug() << "New folder hash:" << CFileSystemObject(newFolderPath).hash();
		setCursorPositionForCurrentFolder(activePanelPosition(), CFileSystemObject(newFolderPath).hash());
	}

	if (!parentDir.mkpath(name))
	{
		// Restore the previous current item in case of failure
		setCursorPositionForCurrentFolder(activePanelPosition(), currentItemHash);
		return false;
	}
	else
		return true;
}

bool CController::createFile(const QString &parentFolder, const QString &name)
{
	CFileSystemObject parentDir(parentFolder);
	if (!parentDir.exists() || !parentDir.isDir())
		return false;

	const QString newFilePath = parentDir.fullAbsolutePath() % '/' % name;
	if (QFile(newFilePath).open(QFile::WriteOnly))
	{
		if (parentDir.fullAbsolutePath() == activePanel().currentDirPathPosix())
			// This is required for the UI to know to set the cursor at the new file
			setCursorPositionForCurrentFolder(activePanelPosition(), CFileSystemObject(newFilePath).hash());

		return true;
	}
	else
		return false;
}

void CController::openTerminal(const QString &folder, bool admin)
{
#if defined __APPLE__
	system(QString("osascript -e \"tell application \\\"Terminal\\\" to do script \\\"cd %1\\\"\"").arg(folder).toUtf8().data());
#elif defined __linux__ || defined _WIN32
	if (!admin)
	{
		const bool started = CShell::runExecutable(CShell::shellExecutable(), QString(), folder);
		assert_r(started);
	}
	else
	{
#ifdef _WIN32
		assert_r(CShell::runExeAsAdmin(CShell::shellExecutable(), folder));
#endif
	}
#else
	#error unknown platform
#endif

	Q_UNUSED(admin);
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

// Indicates that we need to move cursor (e. g. a folder is being renamed and we want to keep the cursor on it)
// This method takes the current folder in the currently active panel
void CController::setCursorPositionForCurrentFolder(Panel p, qulonglong newCurrentItemHash)
{
	panel(p).setCurrentItemForFolder(panel(p).currentDirPathPosix(), newCurrentItemHash);
	CPluginEngine::get().currentItemChanged(activePanelPosition(), newCurrentItemHash);
}

void CController::copyCurrentItemToClipboard()
{
	const auto item = currentItem();
	if (item.isValid())
		QApplication::clipboard()->setText(toNativeSeparators(item.fullAbsolutePath()));
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
		assert_unconditional_r("Unknown panel");
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
		assert_unconditional_r("Unknown panel");
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
		assert_unconditional_r("Uknown panel");
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
		assert_unconditional_r("Uknown panel");
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
		assert_unconditional_r("Uknown panel");
		return LeftPanel;
	}
}


Panel CController::activePanelPosition() const
{
	assert_r(_activePanel == RightPanel || _activePanel == LeftPanel);
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

CFileSystemObject CController::itemByHash( Panel p, qulonglong hash ) const
{
	return panel(p).itemByHash(hash);
}

std::vector<CFileSystemObject> CController::items(Panel p, const std::vector<qulonglong>& hashes) const
{
	std::vector<CFileSystemObject> objects;
	std::for_each(hashes.begin(), hashes.end(), [&objects, p, this] (qulonglong hash) {objects.push_back(itemByHash(p, hash));});
	return objects;
}

QString CController::itemPath(Panel p, qulonglong hash) const
{
	return panel(p).itemByHash(hash).properties().fullPath;
}

CDiskEnumerator& CController::diskEnumerator()
{
	return _diskEnumerator;
}

QString CController::diskPath(size_t index) const
{
	return index < _diskEnumerator.drives().size() ? _diskEnumerator.drives()[index].fileSystemObject.fullAbsolutePath() : QString();
}

size_t CController::currentDiskIndex(Panel p) const
{
	const auto& drives = _diskEnumerator.drives();
	for (size_t i = 0; i < drives.size(); ++i)
	{
		if (CFileSystemObject(panel(p).currentDirPathNative()).isChildOf(drives[i].fileSystemObject))
			return i;
	}

	return std::numeric_limits<int>::max();
}

CFavoriteLocations& CController::favoriteLocations()
{
	return _favoriteLocations;
}

CFileSearchEngine& CController::fileSearchEngine()
{
	return _fileSearchEngine;
}

// Returns hash of an item that was the last selected in the specified dir
qulonglong CController::currentItemHashForFolder(Panel p, const QString &dir) const
{
	return panel(p).currentItemForFolder(dir);
}

qulonglong CController::currentItemHash()
{
	return activePanel().currentItemForFolder(activePanel().currentDirPathPosix());
}

CFileSystemObject CController::currentItem()
{
	return activePanel().itemByHash(currentItemHash());
}

void CController::disksChanged()
{
	const auto& drives = _diskEnumerator.drives();

	_rightPanel.disksChanged(drives);
	_leftPanel.disksChanged(drives);

	for (auto& listener: _disksChangedListeners)
	{
		listener->disksChanged(drives, RightPanel);
		listener->disksChanged(drives, LeftPanel);
	}
}

void CController::saveDirectoryForCurrentDisk(Panel p)
{
	const CFileSystemObject path(panel(p).currentDirPathNative());
	if (path.isNetworkObject())
		return;

	assert_and_return_r(path.isNetworkObject() || currentDiskIndex(p) < _diskEnumerator.drives().size(), );

	const QString drivePath = _diskEnumerator.drives().at(currentDiskIndex(p)).storageInfo.rootPath();
	CSettings().setValue(p == LeftPanel ? KEY_LAST_PATH_FOR_DRIVE_L.arg(drivePath.toHtmlEscaped()) : KEY_LAST_PATH_FOR_DRIVE_R.arg(drivePath.toHtmlEscaped()), path.fullAbsolutePath());
}
