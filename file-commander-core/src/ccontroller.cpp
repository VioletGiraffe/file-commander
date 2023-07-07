#include "ccontroller.h"
#include "settings/csettings.h"
#include "settings.h"
#include "shell/cshell.h"
#include "pluginengine/cpluginengine.h"
#include "filesystemhelperfunctions.h"
#include "iconprovider/ciconprovider.h"

#include "qtcore_helpers/qstring_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <iterator>

CController* CController::_instance = nullptr;

CController::CController() :
	_favoriteLocations{KEY_FAVORITES},
	_fileSearchEngine{*this},
	_leftPanel{LeftPanel},
	_rightPanel{RightPanel},
	_pluginProxy{[this](const std::function<void()>& code) {execOnUiThread(code);}},
	_workerThreadPool{2, "CController thread pool"}
{
	assert_r(_instance == nullptr); // Only makes sense to create one controller
	_instance = this;

	_volumeEnumerator.addObserver(this);

	_leftPanel.addPanelContentsChangedListener(&CPluginEngine::get());
	_rightPanel.addPanelContentsChangedListener(&CPluginEngine::get());

	// Manual update for the CPanels to get the volumes list
	_volumeEnumerator.updateSynchronously();

	_leftPanel.restoreFromSettings();
	_rightPanel.restoreFromSettings();

	_volumeEnumerator.startEnumeratorThread();
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

void CController::setVolumesChangedListener(CController::IVolumeListObserver *listener)
{
	assert_r(std::find(_volumesChangedListeners.begin(), _volumesChangedListeners.end(), listener) == _volumesChangedListeners.end());
	_volumesChangedListeners.push_back(listener);

	// Force an update
	volumesChanged(true /* Significant change */);
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
	// Tabs not yet implemented
}

// Indicates that an item was activated and appropriate action should be taken. Returns error message, if any
FileOperationResultCode CController::itemActivated(qulonglong itemHash, Panel p)
{
	const auto item = panel(p).itemByHash(itemHash);
	if (item.isBundle())
	{
		// macOS bundle: launch it as an application
		return QDesktopServices::openUrl(QUrl::fromLocalFile(item.fullAbsolutePath())) ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
	}
	else if (item.isDir())
	{
		// Attempting to enter this dir
		const FileOperationResultCode result = setPath(p, item.fullAbsolutePath(), item.isCdUp() ? refreshCauseCdUp : refreshCauseForwardNavigation);
		return result;
	}
	else if (item.isFile())
	{
		if (item.isExecutable())
			// Attempting to launch this exe from the current directory
			return OsShell::runExecutable(item.fullAbsolutePath(), QString(), item.parentDirPath()) ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
		else
			// It's probably not a binary file, try opening with openUrl
			return QDesktopServices::openUrl(QUrl::fromLocalFile(item.fullAbsolutePath())) ? FileOperationResultCode::Ok : FileOperationResultCode::Fail;
	}

	return FileOperationResultCode::Fail;
}

// A current volume has been switched
std::pair<bool /*success*/, QString/*volume root path*/> CController::switchToVolume(Panel p, uint64_t id)
{
	const QString drivePath = volumePathById(id);

	const auto currentVolume = currentVolumeInfo(otherPanelPosition(p));
	// When switching to the same volume that is selected in the other panel, also navigate to the same directory that's currently selected
	if (currentVolume && drivePath == currentVolume->rootObjectInfo.fullAbsolutePath())
	{
		return {setPath(p, otherPanel(p).currentDirPathPosix(), refreshCauseOther) == FileOperationResultCode::Ok, drivePath};
	}
	else
	{
		// Otherwise navigate to the last known path for this volume, or its root if no path was recorded previously
		const QString lastPathForDrive = CSettings{}.value(p == LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(drivePath.toHtmlEscaped()) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(drivePath.toHtmlEscaped()), drivePath).toString();
		return {setPath(p, toPosixSeparators(lastPathForDrive), refreshCauseOther) == FileOperationResultCode::Ok, drivePath};
	}
}

// Porgram settings have changed
void CController::settingsChanged()
{
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
	// TODO: this looks weird, a separate notification required?
	volumesChanged(false); // To select a proper drive button
	saveDirectoryForCurrentVolume(p);
}

// Go to the previous location from history, if any
void CController::navigateBack(Panel p)
{
	panel(p).navigateBack();
	saveDirectoryForCurrentVolume(p);
	// TODO: this looks weird, a separate notification required?
	volumesChanged(false); // To select a proper drive button
}

// Go to the next location from history, if any
void CController::navigateForward(Panel p)
{
	panel(p).navigateForward();
	saveDirectoryForCurrentVolume(p);
	// TODO: this looks weird, a separate notification required?
	volumesChanged(false); // To select a proper drive button
}

// Sets the specified path, if possible. Otherwise reverts to the previously set path
FileOperationResultCode CController::setPath(Panel p, const QString &path, FileListRefreshCause operation)
{
	const FileOperationResultCode result = panel(p).setPath(path, operation);

	saveDirectoryForCurrentVolume(p);
	// TODO: this looks weird, a separate notification required?
	volumesChanged(false); // To select a proper drive button
	return result;
}

FileOperationResultCode CController::createFolder(const QString &parentFolder, const QString &name)
{
	QDir parentDir(parentFolder);
	if (!parentDir.exists())
		return FileOperationResultCode::Fail;

	const auto currentItemHash = currentItemHashForFolder(_activePanel, parentDir.absolutePath());

	// Comparing with CFileSystemObject{parentFolder} instead of parentDir to avoid potential slash direction and trailing slash issues for paths coming from different APIs
	if (CFileSystemObject{parentFolder}.fullAbsolutePath() == activePanel().currentDirObject().fullAbsolutePath())
	{
		const auto slashPosition = name.indexOf('/');
		// The trailing slash is required in order for the hash to match the hash of the item once it will be created: existing folders always have a trailing hash
		const QString newItemPath = parentDir.absolutePath() % '/' % (slashPosition > 0 ? name.left(slashPosition) : name) % '/';
		// This is required for the UI to know to set the cursor at the new folder.
		// It must be done before calling mkpath, or #133 will occur due to asynchronous file list refresh between mkpath and the current item selection logic (it gets overwritten from CPanelWidget::fillFromList).
		const auto newHash = CFileSystemObject(newItemPath).hash();
		qInfo() << "New folder hash:" << newHash;
		setCursorPositionForCurrentFolder(activePanelPosition(), newHash, false);
	}

	if (parentDir.exists(name))
		return FileOperationResultCode::TargetAlreadyExists;

	if (!parentDir.mkpath(name))
	{
		// Restore the previous current item in case of failure
		setCursorPositionForCurrentFolder(activePanelPosition(), currentItemHash);
		return FileOperationResultCode::Fail;
	}
	else
		return FileOperationResultCode::Ok;
}

FileOperationResultCode CController::createFile(const QString &parentFolder, const QString &name)
{
	CFileSystemObject parentDir(parentFolder);
	if (!parentDir.exists() || !parentDir.isDir())
		return FileOperationResultCode::Fail;

	if (parentDir.fullAbsolutePath() == activePanel().currentDirObject().fullAbsolutePath())
	{
		const auto slashPosition = name.indexOf('/');
		// The trailing slash is required in order for the hash to match the hash of the item once it will be created: existing folders always have a trailing hash
		const QString newItemPath = parentDir.fullAbsolutePath() % '/' % (slashPosition > 0 ? name.left(slashPosition) : name) % '/';
		// This is required for the UI to know to set the cursor at the new folder.
		// It must be done before calling mkpath, or #133 will occur due to asynchronous file list refresh between mkpath and the current item selection logic (it gets overwritten from CPanelWidget::fillFromList).
		const auto newHash = CFileSystemObject(newItemPath).hash();
		qInfo() << "New file hash:" << newHash;
		setCursorPositionForCurrentFolder(activePanelPosition(), newHash);
	}

	const QString newFilePath = parentDir.fullAbsolutePath() % '/' % name;
	if (QFile::exists(newFilePath))
		return FileOperationResultCode::TargetAlreadyExists;

	if (QFile(newFilePath).open(QFile::WriteOnly))
	{
		if (parentDir.fullAbsolutePath() == activePanel().currentDirPathPosix())
			// This is required for the UI to know to set the cursor at the new file
			setCursorPositionForCurrentFolder(activePanelPosition(), CFileSystemObject(newFilePath).hash());

		return FileOperationResultCode::Ok;
	}

	return FileOperationResultCode::Fail;
}

void CController::openTerminal(const QString &folder, bool admin)
{
#if defined __APPLE__
	// Regular escaping with "\ " doesn't work here, need to use single quaotes
	const auto script = QString(R"(osascript -e "tell application \"Terminal\" to do script \"cd %1\"")").arg(escapedPath(folder));
	system(script.toUtf8().constData());
	Q_UNUSED(admin);
#elif defined __linux__ || defined __FreeBSD__ || defined _WIN32
	if (!admin)
	{
		auto [terminalProgram, args] = OsShell::shellExecutable();
		static constexpr auto* dirTemplate = "%dir%";
		if (args.contains(dirTemplate))
		{
			args.replace(
				dirTemplate,
				escapedPath(!folder.endsWith(nativeSeparator()) ? folder : QString{folder}.remove(folder.size() - 1, 1))
			);
		}

		const bool started = OsShell::runExecutable(terminalProgram, args, folder);
		assert_r(started);
	}
	else
	{
#ifdef _WIN32
		const auto terminalProgramArgs = OsShell::shellExecutable();
		const QString& terminalProgram = terminalProgramArgs.first;
		QString arguments;
		if (terminalProgram.contains(QSL("powershell"), Qt::CaseInsensitive))
			arguments = QSL("-noexit -command \"cd \"\"%1\"\" \"").arg(toNativeSeparators(folder));
		else if (terminalProgram.toLower() == QSL("cmd") || terminalProgram.toLower() == QSL("cmd.exe"))
			arguments = QSL("/k \"cd /d %1 \"").arg(toNativeSeparators(folder));

		if (!arguments.isEmpty() && !terminalProgramArgs.second.isEmpty())
			arguments += ' ';
		arguments.prepend(terminalProgramArgs.second);

		assert_r(OsShell::runExe(terminalProgram, arguments, folder, true));
#endif
	}
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

// Indicates that we need to move cursor (e. g. a folder is being renamed and we want to keep the cursor on it)
// This method takes the current folder in the currently active panel
void CController::setCursorPositionForCurrentFolder(Panel p, qulonglong newCurrentItemHash, const bool notifyUi)
{
	panel(p).setCurrentItemForFolder(panel(p).currentDirPathPosix(), newCurrentItemHash, notifyUi);
	CPluginEngine::get().currentItemChanged(activePanelPosition(), newCurrentItemHash);
}

void CController::copyCurrentItemPathToClipboard()
{
	const auto item = currentItem();
	if (item.isValid())
		QApplication::clipboard()->setText(escapedPath(toNativeSeparators(item.fullAbsolutePath())));
}

void CController::execOnWorkerThread(std::function<void ()> task)
{
	_workerThreadPool.enqueue(std::move(task));
}

void CController::execOnUiThread(std::function<void ()> task, int tag)
{
	_uiQueue.enqueue(std::move(task), tag);
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

std::vector<VolumeInfo> CController::volumes() const
{
	return _volumeEnumerator.volumes();
}

std::optional<VolumeInfo> CController::currentVolumeInfo(Panel p) const
{
	const auto currentDirectoryObject = CFileSystemObject(panel(p).currentDirPathNative());
	return volumeInfoForObject(currentDirectoryObject);
}

std::optional<VolumeInfo> CController::volumeInfoForObject(const CFileSystemObject &object) const noexcept
{
	const auto volumes = _volumeEnumerator.volumes();
	const auto infoIt = std::find_if(cbegin_to_end(volumes), [&object](const VolumeInfo& item) {return item.rootObjectInfo.rootFileSystemId() == object.rootFileSystemId();});

	if (infoIt != volumes.cend())
		return *infoIt;
	else
		return {};
}

std::optional<VolumeInfo> CController::volumeInfoById(uint64_t id) const
{
	return _volumeEnumerator.volumeById(id);
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

void CController::volumesChanged(bool drivesListOrReadinessChanged) noexcept
{
	const auto drives = _volumeEnumerator.volumes();

	for (auto& listener: _volumesChangedListeners)
	{
		listener->volumesChanged(drives, RightPanel, drivesListOrReadinessChanged);
		listener->volumesChanged(drives, LeftPanel, drivesListOrReadinessChanged);
	}
}

QString CController::volumePathById(uint64_t id) const
{
	const auto info = _volumeEnumerator.volumeById(id);
	return info ? info->rootObjectInfo.fullAbsolutePath() : QString{};
}

void CController::saveDirectoryForCurrentVolume(Panel p)
{
	const CFileSystemObject path(panel(p).currentDirPathNative());
	if (path.isNetworkObject())
		return;

	const auto currentVolume = currentVolumeInfo(p);
	assert_and_return_r(currentVolume, );

	const QString drivePath = currentVolume->rootObjectInfo.fullAbsolutePath();
	CSettings().setValue(p == LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(drivePath.toHtmlEscaped()) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(drivePath.toHtmlEscaped()), path.fullAbsolutePath());
}
