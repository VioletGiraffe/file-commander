#include "ccontroller.h"
#include "settings/csettings.h"
#include "settings.h"
#include "shell/cshell.h"
#include "pluginengine/cpluginengine.h"
#include "filesystemhelperfunctions.h"
#include "filesystemhelpers/filestatistics.h"
#include "iconprovider/ciconprovider.h"

#include "qtcore_helpers/qstring_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <thread>

CController* CController::_instance = nullptr;

CController::CController() :
	_favoriteLocations{KEY_FAVORITES},
	_panelWorkerPool{ std::min(4u, std::thread::hardware_concurrency()), "Panel file list pool" },
	_pluginProxy{[this](const std::function<void()>& code) {execOnUiThread(code);}},
	_workerThreadPool{2, "CController thread pool"}
{
	assert_r(_instance == nullptr); // Only makes sense to create one controller
	_instance = this;

	_volumeEnumerator.addObserver(this);

	// Volumes must be enumerated before restoring panel paths: setPath checks path accessibility against the volume list.
	_volumeEnumerator.updateSynchronously();

	// Rebuild each side's tabs from settings (migrating from the legacy single-path keys on first run).
	for (const Panel p : { Panel::LeftPanel, Panel::RightPanel })
		restorePanelState(p);

	_volumeEnumerator.startEnumeratorThread();

	_wcxHost.setWcxSearchPath(QApplication::applicationDirPath());
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
	// Record it so tabs created later also get it, and attach it to every existing tab of this side.
	_panelContentsListeners[(size_t)p].push_back(listener);
	for (auto& tab : _panels[(size_t)p].tabs)
		tab->addPanelContentsChangedListener(listener);
}

void CController::setCursorPositionListener(Panel p, CursorPositionListener *listener)
{
	_cursorPositionListeners[(size_t)p].push_back(listener);
	for (auto& tab : _panels[(size_t)p].tabs)
		tab->addCurrentItemChangeListener(listener);
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
	// Tick every tab, not just the active one: a tab that was switched away from may still have an in-flight refresh
	// whose result is queued in its UI-thread queue and needs to be drained.
	for (auto& tabList : _panels)
	{
		for (auto& tab : tabList.tabs)
			tab->uiThreadTimerTick();
	}

	_uiQueue.exec(CExecutionQueue::execAll);
}

// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
void CController::refreshPanelContents(Panel p)
{
	panel(p).refreshFileList(refreshCauseOther);
}

CPanel& CController::createTab(Panel p)
{
	auto& tabList = _panels[(size_t)p];
	CPanel& tab = *tabList.tabs.emplace_back(std::make_unique<CPanel>(p, _panelWorkerPool, _nextTabId++));
	attachListenersToTab(p, tab);
	return tab;
}

void CController::attachListenersToTab(Panel p, CPanel& tab)
{
	tab.addPanelContentsChangedListener(&CPluginEngine::get());
	tab.addPanelContentsChangedListener(this); // The controller listens to persist tab state on navigation (deduped)
	for (auto* listener : _panelContentsListeners[(size_t)p])
		tab.addPanelContentsChangedListener(listener);
	for (auto* listener : _cursorPositionListeners[(size_t)p])
		tab.addCurrentItemChangeListener(listener);
}

TabId CController::addTab(Panel p, const QString& path, bool activate)
{
	CPanel& tab = createTab(p);
	tab.setPath(path, refreshCauseOther);
	const TabId newId = tab.id();

	if (activate)
		switchActiveTab(p, newId);
	else
		tab.setActive(false); // Not the active tab: release the watch handle setPath() just armed

	savePanelState(p);
	return newId;
}

void CController::closeTab(Panel p, TabId tabId)
{
	auto& tabList = _panels[(size_t)p];
	if (tabList.tabs.size() == 1)
		return; // A panel always keeps at least one tab

	const size_t idx = tabIndexById(p, tabId);
	assert_and_return_r(idx < tabList.tabs.size(), );
	const bool wasActive = (idx == tabList.activeTab);

	tabList.tabs[idx]->setActive(false); // Release its watch handle before destruction
	tabList.tabs.erase(tabList.tabs.begin() + (ptrdiff_t)idx);

	if (tabList.activeTab > idx)
		--tabList.activeTab; // The active tab shifted left but is still the same tab
	else if (wasActive)
	{
		// The active tab was the one removed: clamp the index and activate whatever now occupies the slot.
		if (tabList.activeTab >= tabList.tabs.size())
			tabList.activeTab = tabList.tabs.size() - 1;
		tabList.tabs[tabList.activeTab]->setActive(true);
	}

	savePanelState(p);
}

void CController::setActiveTab(Panel p, TabId tabId)
{
	auto& tabList = _panels[(size_t)p];
	if (tabList.tabs[tabList.activeTab]->id() == tabId)
		return;

	switchActiveTab(p, tabId);
	savePanelState(p);
}

void CController::switchActiveTab(Panel p, TabId tabId)
{
	auto& tabList = _panels[(size_t)p];
	const size_t idx = tabIndexById(p, tabId);
	assert_and_return_r(idx < tabList.tabs.size(), );

	tabList.tabs[tabList.activeTab]->setActive(false);
	tabList.activeTab = idx;
	tabList.tabs[tabList.activeTab]->setActive(true);
}

size_t CController::tabIndexById(Panel p, TabId tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	for (size_t i = 0; i < tabList.tabs.size(); ++i)
		if (tabList.tabs[i]->id() == tabId)
			return i;
	return tabList.tabs.size();
}

int CController::tabCount(Panel p) const
{
	return (int)_panels[(size_t)p].tabs.size();
}

TabId CController::activeTabId(Panel p) const
{
	const auto& tabList = _panels[(size_t)p];
	return tabList.tabs[tabList.activeTab]->id();
}

std::vector<TabId> CController::tabIds(Panel p) const
{
	const auto& tabList = _panels[(size_t)p];
	std::vector<TabId> ids;
	ids.reserve(tabList.tabs.size());
	for (const auto& tab : tabList.tabs)
		ids.push_back(tab->id());
	return ids;
}

QString CController::tabPath(Panel p, TabId tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	const size_t idx = tabIndexById(p, tabId);
	assert_and_return_r(idx < tabList.tabs.size(), QString());
	return tabList.tabs[idx]->currentDirPathPosix();
}

QString CController::tabName(Panel p, TabId tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	const size_t idx = tabIndexById(p, tabId);
	assert_and_return_r(idx < tabList.tabs.size(), QString());
	return tabList.tabs[idx]->currentDirName();
}

void CController::restorePanelState(Panel p)
{
	const size_t side = (size_t)p;
	CSettings s;

	QStringList tabPaths = s.value(p == Panel::LeftPanel ? KEY_LPANEL_TABS : KEY_RPANEL_TABS).toStringList();
	if (tabPaths.isEmpty())
	{
		// Migration from the pre-tabs single-path setting (also the first-run default): one tab at the legacy location.
		tabPaths.push_back(s.value(p == Panel::LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::homePath()).toString());
	}

	int activeIndex = s.value(p == Panel::LeftPanel ? KEY_LPANEL_ACTIVE_TAB : KEY_RPANEL_ACTIVE_TAB, 0).toInt();
	if (activeIndex < 0 || activeIndex >= tabPaths.size())
		activeIndex = 0;

	// v1 restores only the active tab's history (saved under the legacy key).
	const QStringList historyList = s.value(p == Panel::LeftPanel ? KEY_HISTORY_L : KEY_HISTORY_R).toStringList();
	const std::vector<QString> history(historyList.cbegin(), historyList.cend());

	auto& tabList = _panels[side];
	for (int i = 0; i < tabPaths.size(); ++i)
	{
		CPanel& tab = createTab(p);
		if (i == activeIndex)
			tab.restoreHistory(history);
		tab.setPath(tabPaths[i], refreshCauseOther);
	}
	tabList.activeTab = (size_t)activeIndex;

	// createTab() + setPath() armed each tab's watcher; release it on every inactive tab.
	for (size_t i = 0; i < tabList.tabs.size(); ++i)
		if (i != tabList.activeTab)
			tabList.tabs[i]->setActive(false);
}

void CController::savePanelState(Panel p)
{
	const size_t side = (size_t)p;
	const auto& tabList = _panels[side];
	if (tabList.tabs.empty())
		return;

	QStringList tabPaths;
	for (const auto& tab : tabList.tabs)
		tabPaths.push_back(tab->currentDirPathPosix());
	const int activeIndex = (int)tabList.activeTab;

	// Dedup: contents-changed also fires on watcher refreshes, where nothing relevant to persistence changed.
	QString signature = QString::number(activeIndex);
	for (const QString& path : tabPaths)
	{
		signature += QChar('\n');
		signature += path;
	}
	if (signature == _lastSavedTabSignature[side])
		return;
	_lastSavedTabSignature[side] = signature;

	CSettings s;
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_TABS : KEY_RPANEL_TABS, tabPaths);
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_ACTIVE_TAB : KEY_RPANEL_ACTIVE_TAB, activeIndex);

	// Mirror the active tab's path + history to the legacy keys (back-compat + crash recovery).
	const CPanel& activeTab = *tabList.tabs[tabList.activeTab];
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, tabPaths[activeIndex]);
	const auto& historyDeque = activeTab.history().list();
	s.setValue(p == Panel::LeftPanel ? KEY_HISTORY_L : KEY_HISTORY_R, QStringList(historyDeque.cbegin(), historyDeque.cend()));
}

void CController::onPanelContentsChanged(Panel p, FileListRefreshCause /*operation*/)
{
	savePanelState(p);
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
		const QString lastPathForDrive = CSettings{}.value(p == Panel::LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(drivePath.toHtmlEscaped()) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(drivePath.toHtmlEscaped()), drivePath).toString();
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
		args.replace(
			dirTemplate,
			escapedPath(!folder.endsWith(nativeSeparator()) ? folder : QString{folder}.remove(folder.size() - 1, 1))
		);

		const bool started = OsShell::runExecutable(terminalProgram, args, folder);
		assert_r(started);
	}
	else
	{
#ifdef _WIN32
		auto terminalProgramArgs = OsShell::shellExecutable();
		const QString& terminalProgram = terminalProgramArgs.first;
		QString arguments;
		if (terminalProgram.contains(QSL("powershell"), Qt::CaseInsensitive))
			arguments = QSL("-noexit -command \"cd \"\"%1\"\" \"").arg(toNativeSeparators(folder));
		else if (terminalProgram.toLower() == QSL("cmd") || terminalProgram.toLower() == QSL("cmd.exe"))
			arguments = QSL("/k \"cd /d %1 \"").arg(toNativeSeparators(folder));

		static constexpr auto* dirTemplate = "%dir%";
		terminalProgramArgs.second.replace(
			dirTemplate,
			escapedPath(!folder.endsWith(nativeSeparator()) ? folder : QString{ folder }.remove(folder.size() - 1, 1))
		);

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
	case Panel::LeftPanel:
	case Panel::RightPanel:
	{
		const TabList& tabList = _panels[(size_t)p];
		return *tabList.tabs[tabList.activeTab];
	}
	default:
		assert_unconditional_r("Unknown panel");
		return *_panels[(size_t)Panel::RightPanel].tabs[_panels[(size_t)Panel::RightPanel].activeTab];
	}
}

CPanel& CController::panel(Panel p)
{
	switch (p)
	{
	case Panel::LeftPanel:
	case Panel::RightPanel:
	{
		TabList& tabList = _panels[(size_t)p];
		return *tabList.tabs[tabList.activeTab];
	}
	default:
		assert_unconditional_r("Unknown panel");
		return *_panels[(size_t)Panel::RightPanel].tabs[_panels[(size_t)Panel::RightPanel].activeTab];
	}
}

const CPanel &CController::otherPanel(Panel p) const
{
	return panel(otherPanelPosition(p));
}

CPanel& CController::otherPanel(Panel p)
{
	return panel(otherPanelPosition(p));
}


Panel CController::otherPanelPosition(Panel p)
{
	switch (p)
	{
	case Panel::LeftPanel:
		return Panel::RightPanel;
	case Panel::RightPanel:
		return Panel::LeftPanel;
	default:
		assert_unconditional_r("Uknown panel");
		return Panel::LeftPanel;
	}
}


Panel CController::activePanelPosition() const
{
	assert_r(_activePanel == Panel::RightPanel || _activePanel == Panel::LeftPanel);
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
	objects.reserve(hashes.size());

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
		listener->volumesChanged(drives, Panel::RightPanel, drivesListOrReadinessChanged);
		listener->volumesChanged(drives, Panel::LeftPanel, drivesListOrReadinessChanged);
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
	CSettings().setValue(p == Panel::LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(drivePath.toHtmlEscaped()) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(drivePath.toHtmlEscaped()), path.fullAbsolutePath());
}
