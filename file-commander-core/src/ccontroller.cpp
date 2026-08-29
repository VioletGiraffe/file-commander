#include "ccontroller.h"
#include "settings.h"
#include "shell/cshell.h"
#include "filesystemhelperfunctions.h"
#include "filesystemhelpers/filestatistics.h"
#include "iconprovider/ciconprovider.h"
#include "threading/thread_helpers.h"

#include "qtcore_helpers/qstring_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>

CController* CController::_instance = nullptr;

namespace {
	// One persisted tab, as stored in a single KEY_*PANEL_TABS list entry.
	struct PersistedTab {
		qulonglong id = 0; // 0 when the entry predates ids: the caller assigns a fresh one
		qulonglong cursorHash = 0;
		QString path;
	};

	[[nodiscard]] QString encodeTabRecord(qulonglong id, qulonglong cursorHash, const QString& path) {
		return QString::number(id) + ':' + QString::number(cursorHash) + ':' + path;
	}

	// A stored path is always absolute, so it cannot imitate the "<digits>:<digits>:" prefix: on Windows the drive
	// is a letter, elsewhere the path starts with '/'. Anything that fails to match is therefore a pre-id record.
	[[nodiscard]] PersistedTab parseTabRecord(const QString& record) {
		const qsizetype idEnd = record.indexOf(':');
		const qsizetype cursorEnd = idEnd > 0 ? record.indexOf(':', idEnd + 1) : -1;
		if (cursorEnd <= idEnd)
			return { 0, 0, record };

		bool idParsed = false, cursorParsed = false;
		const qulonglong id = QStringView{ record }.left(idEnd).toULongLong(&idParsed);
		const qulonglong cursorHash = QStringView{ record }.sliced(idEnd + 1, cursorEnd - idEnd - 1).toULongLong(&cursorParsed);
		if (!idParsed || !cursorParsed || id == 0)
			return { 0, 0, record };

		return { id, cursorHash, record.sliced(cursorEnd + 1) };
	}

	inline uint32_t optimalCpuCount() {
		const auto cpu = CpuCount::get();
		uint32_t optimalCores = std::max(cpu.performanceCoreCount(), cpu.efficiencyCoreCount());
		if (optimalCores > 1 && optimalCores == cpu.logicalProcessorCount())
			optimalCores -= 1; // Leave one core free for the UI thread and other system tasks

		return optimalCores;
	}
}

CController::CController() :
	_favoriteLocations{KEY_FAVORITES},
	_panelWorkerPool{ std::clamp(std::thread::hardware_concurrency(), 1u, 4u), "Panel file list pool" },
	_workerPool{ optimalCpuCount(), "Application pool"}
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

	const auto cpu = CpuCount::get();
	qInfo().nospace() << "CPU cores: " << cpu.performanceCoreCount() << "P+" << cpu.efficiencyCoreCount() << "E, " << cpu.logicalProcessorCount() << " logical, worker pool: " << _workerPool.maxWorkersCount();
}

CController::~CController()
{
	assert_r(_panels[(size_t)Panel::LeftPanel].tabs.empty());
	assert_r(_panels[(size_t)Panel::RightPanel].tabs.empty());

	_workerPool.finishAllThreads(false);

	_instance = nullptr;
}

CController& CController::get()
{
	assert_r(_instance);
	return *_instance;
}

void CController::shutdown()
{
	assert_r(QThread::currentThread() == qApp->thread());
	std::unique_lock lock(_pluginAccessMutex);
	assert_r(!_panels[(size_t)Panel::LeftPanel].tabs.empty());
	assert_r(!_panels[(size_t)Panel::RightPanel].tabs.empty());

	// Capture the final state (notably each tab's cursor position, which pure cursor moves don't otherwise persist)
	// before stopping and destroying the panels.
	for (const Panel p : { Panel::LeftPanel, Panel::RightPanel })
		savePanelState(p);
	saveHistory();

	// One-time cleanup: the cursor hashes now live inside the tab records savePanelState just wrote. Removing a key
	// that isn't there still dirties the settings, so check first - this runs on every exit for the rest of time.
	QSettings legacyCursorKeys;
	for (const QString& key : { KEY_LPANEL_TAB_CURSORS, KEY_RPANEL_TAB_CURSORS })
	{
		if (legacyCursorKeys.contains(key))
			legacyCursorKeys.remove(key);
	}

	_volumeEnumerator.shutdown();
	_iconProvider.shutdown();

	// CPanel destruction raises its abort flag before retiring the panel's tagged work, so an in-flight recursive
	// scan exits promptly. The shared pool must remain alive until every panel has completed that handshake.
	for (auto& tabList : _panels)
		tabList.tabs.clear();
	_panelWorkerPool.finishAllThreads(false);
	_activePanel = Panel::UnknownPanel;
	for (auto& selection : _selectedItemsHashes)
		selection.clear();

	for (auto& listeners : _panelContentsListeners)
		listeners.clear();
	for (auto& listeners : _currentItemChangedListeners)
		listeners.clear();
	_volumesChangedListeners.clear();
	// Move external callables out while access is exclusive, then destroy them after releasing the gate: capture
	// destructors are external code and must be free to re-enter the controller without deadlocking.
	CPluginProxy::CreateToolMenuEntryImplementationType retiredMenuImplementation;
	retiredMenuImplementation.swap(_createToolMenuEntryImplementation);
	std::vector<PanelContentsSubscription> retiredPanelSubscriptions;
	retiredPanelSubscriptions.swap(_panelContentsSubscriptions);

	lock.unlock();
	_uiQueue.clear();
}

void CController::setToolMenuEntryCreatorImplementation(const CPluginProxy::CreateToolMenuEntryImplementationType& implementation)
{
	assert_r(QThread::currentThread() == qApp->thread());
	_createToolMenuEntryImplementation = implementation;
}

void CController::createToolMenuEntries(const std::vector<CPluginProxy::MenuTree>& menuTrees)
{
	assert_r(QThread::currentThread() == qApp->thread());

	CPluginProxy::CreateToolMenuEntryImplementationType implementation;
	{
		std::shared_lock lock(_pluginAccessMutex);
		implementation = _createToolMenuEntryImplementation;
	}

	if (implementation)
		implementation(menuTrees);
}

void CController::subscribeToPanelContents(const CPluginProxy* subscriber, PanelContentsSubscriber callback)
{
	assert_r(QThread::currentThread() == qApp->thread());
	assert_r(std::find_if(_panelContentsSubscriptions.begin(), _panelContentsSubscriptions.end(),
		[subscriber](const auto& subscription) { return subscription.subscriber == subscriber; }) == _panelContentsSubscriptions.end());

	{
		std::shared_lock lock(_pluginAccessMutex);
		if (_panels[(size_t)Panel::LeftPanel].tabs.empty() || _panels[(size_t)Panel::RightPanel].tabs.empty())
			return; // The panels are gone, and nothing will ever be published again

		_panelContentsSubscriptions.emplace_back(PanelContentsSubscription{ subscriber, std::move(callback) });
	}

	const PanelContentsSubscriber initialDeliveryCallback = _panelContentsSubscriptions.back().callback;

	// Initial delivery is one stable batch: subscription changes made by the callback apply afterwards.
	for (const Panel p : { Panel::LeftPanel, Panel::RightPanel })
	{
		panel(p).readCommittedContents([&initialDeliveryCallback, p](const QString& folder, const FileListHashMap& contents) {
			initialDeliveryCallback(p, folder, contents);
		});
	}
}

void CController::unsubscribeFromPanelContents(const CPluginProxy* subscriber)
{
	std::erase_if(_panelContentsSubscriptions, [subscriber](const auto& subscription) { return subscription.subscriber == subscriber; });
}

void CController::publishPanelContents(Panel p)
{
	const auto subscriptions = _panelContentsSubscriptions;
	panel(p).readCommittedContents([&subscriptions, p](const QString& folder, const FileListHashMap& contents) {
		for (const auto& subscription : subscriptions)
			subscription.callback(p, folder, contents);
	});
}

void CController::setPanelContentsChangedListener(Panel p, PanelContentsChangedListener *listener)
{
	// Record it so tabs created later also get it, and attach it to every existing tab of this side.
	_panelContentsListeners[(size_t)p].push_back(listener);
	for (auto& tab : _panels[(size_t)p].tabs)
		tab->addPanelContentsChangedListener(listener);
}

void CController::setCurrentItemChangedListener(Panel p, CurrentItemChangedListener *listener)
{
	_currentItemChangedListeners[(size_t)p].push_back(listener);
	for (auto& tab : _panels[(size_t)p].tabs)
		tab->addCurrentItemChangedListener(listener);
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

	_iconProvider.uiThreadTimerTick();
	_uiQueue.exec(CExecutionQueue::execAll);
}

// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
void CController::refreshPanelContents(Panel p)
{
	panel(p).refreshFileList(refreshCauseOther);
}

CPanel& CController::createTab(Panel p, qulonglong id)
{
	auto& tabList = _panels[(size_t)p];
	CPanel& tab = *tabList.tabs.emplace_back(std::make_unique<CPanel>(p, _panelWorkerPool, id));
	attachListenersToTab(p, tab);
	return tab;
}

void CController::attachListenersToTab(Panel p, CPanel& tab)
{
	tab.addPanelContentsChangedListener(this); // The controller listens to persist tab state on navigation (deduped)
	tab.addCurrentPathChangedListener(this);   // ...and to append to the side's visited-locations log on a real path change
	for (auto* listener : _panelContentsListeners[(size_t)p])
		tab.addPanelContentsChangedListener(listener);
	for (auto* listener : _currentItemChangedListeners[(size_t)p])
		tab.addCurrentItemChangedListener(listener);
}

qulonglong CController::addTab(Panel p, const QString& path, bool activate)
{
	std::unique_lock lock(_pluginAccessMutex);
	if (_panels[(size_t)p].tabs.empty())
		return 0;

	CPanel& tab = createTab(p, _nextTabId++);
	tab.setPath(path, refreshCauseOther); // Fires onCurrentPathChanged -> logs the new tab's path to the visited-locations list
	const qulonglong newId = tab.id();

	if (activate)
		switchActiveTab(p, newId);
	else
		tab.setActive(false);

	savePanelState(p);
	return newId;
}

void CController::closeTab(Panel p, qulonglong tabId)
{
	std::unique_lock lock(_pluginAccessMutex);
	auto& tabList = _panels[(size_t)p];
	if (tabList.tabs.empty())
		return;

	if (tabList.tabs.size() == 1)
		return; // A panel always keeps at least one tab

	const auto idx = tabIndexById(p, tabId);
	assert_and_return_r(idx.has_value(), );
	const bool wasActive = (*idx == tabList.activeTab);

	tabList.tabs[*idx]->setActive(false); // Release its watch handle before destruction
	tabList.tabs.erase(tabList.tabs.begin() + (ptrdiff_t)*idx);

	if (tabList.activeTab > *idx)
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

void CController::setActiveTab(Panel p, qulonglong tabId)
{
	std::unique_lock lock(_pluginAccessMutex);
	auto& tabList = _panels[(size_t)p];
	if (tabList.tabs.empty())
		return;

	if (tabList.tabs[tabList.activeTab]->id() == tabId)
		return;

	switchActiveTab(p, tabId);
	savePanelState(p);
}

void CController::switchActiveTab(Panel p, qulonglong tabId)
{
	auto& tabList = _panels[(size_t)p];
	const auto idx = tabIndexById(p, tabId);
	assert_and_return_r(idx.has_value(), );

	tabList.tabs[tabList.activeTab]->setActive(false);
	tabList.activeTab = *idx;
	tabList.tabs[tabList.activeTab]->setActive(true);
}

void CController::moveTabPosition(Panel p, qulonglong tabId, size_t newPosition)
{
	std::unique_lock lock(_pluginAccessMutex);
	auto& tabList = _panels[(size_t)p];
	if (tabList.tabs.empty())
		return;

	const auto idx = tabIndexById(p, tabId);
	assert_and_return_r(idx.has_value(), );
	assert_and_return_r(newPosition < tabList.tabs.size(), );

	const size_t oldPosition = *idx;
	if (oldPosition == newPosition)
		return;

	auto movedTab = std::move(tabList.tabs[oldPosition]);
	tabList.tabs.erase(tabList.tabs.begin() + (ptrdiff_t)oldPosition);
	tabList.tabs.insert(tabList.tabs.begin() + (ptrdiff_t)newPosition, std::move(movedTab));

	// Recompute which slot the active tab now occupies, mirroring the same shift the moved tab just caused.
	if (oldPosition == tabList.activeTab)
		tabList.activeTab = newPosition;
	else if (oldPosition < tabList.activeTab && newPosition >= tabList.activeTab)
		--tabList.activeTab; // the active tab shifted left to fill the gap the move left behind
	else if (oldPosition > tabList.activeTab && newPosition <= tabList.activeTab)
		++tabList.activeTab; // the active tab shifted right to make room for the incoming tab

	savePanelState(p);
}

std::optional<size_t> CController::tabIndexById(Panel p, qulonglong tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	for (size_t i = 0; i < tabList.tabs.size(); ++i)
		if (tabList.tabs[i]->id() == tabId)
			return i;
	return std::nullopt;
}

int CController::tabCount(Panel p) const
{
	return (int)_panels[(size_t)p].tabs.size();
}

qulonglong CController::activeTabId(Panel p) const
{
	const auto& tabList = _panels[(size_t)p];
	return tabList.tabs[tabList.activeTab]->id();
}

std::vector<qulonglong> CController::tabIds(Panel p) const
{
	const auto& tabList = _panels[(size_t)p];
	std::vector<qulonglong> ids;
	ids.reserve(tabList.tabs.size());
	for (const auto& tab : tabList.tabs)
		ids.push_back(tab->id());
	return ids;
}

QString CController::tabPath(Panel p, qulonglong tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	const auto idx = tabIndexById(p, tabId);
	assert_and_return_r(idx.has_value(), QString());
	return tabList.tabs[*idx]->currentDirPathPosix();
}

QString CController::tabName(Panel p, qulonglong tabId) const
{
	const auto& tabList = _panels[(size_t)p];
	const auto idx = tabIndexById(p, tabId);
	assert_and_return_r(idx.has_value(), QString());
	return tabList.tabs[*idx]->currentDirName();
}

const CPanel& CController::tabById(Panel p, qulonglong tabId) const
{
	const auto idx = tabIndexById(p, tabId);
	assert_r(idx.has_value());
	return *_panels[(size_t)p].tabs[idx.value_or(0)]; // A panel always keeps >= 1 tab, so [0] is a safe release-mode fallback
}

void CController::restorePanelState(Panel p)
{
	const size_t side = (size_t)p;
	QSettings s;

	QStringList tabRecords = s.value(p == Panel::LeftPanel ? KEY_LPANEL_TABS : KEY_RPANEL_TABS).toStringList();
	if (tabRecords.isEmpty())
	{
		// Migration from the pre-tabs single-path setting (also the first-run default): one tab at the legacy location.
		tabRecords.push_back(s.value(p == Panel::LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, QDir::homePath()).toString());
	}

	int activeIndex = s.value(p == Panel::LeftPanel ? KEY_LPANEL_ACTIVE_TAB : KEY_RPANEL_ACTIVE_TAB, 0).toInt();
	if (activeIndex < 0 || activeIndex >= tabRecords.size())
		activeIndex = 0;

	// Cursor hashes of pre-id records only, positionally parallel to the tab list they were written with.
	const QStringList legacyCursors = s.value(p == Panel::LeftPanel ? KEY_LPANEL_TAB_CURSORS : KEY_RPANEL_TAB_CURSORS).toStringList();

	std::vector<PersistedTab> persistedTabs;
	persistedTabs.reserve((size_t)tabRecords.size());
	qulonglong highestPersistedId = 0;
	for (int i = 0; i < tabRecords.size(); ++i)
	{
		PersistedTab tab = parseTabRecord(tabRecords[i]);
		if (tab.id == 0 && i < legacyCursors.size())
			tab.cursorHash = legacyCursors[i].toULongLong();

		highestPersistedId = std::max(highestPersistedId, tab.id);
		persistedTabs.push_back(std::move(tab));
	}

	// Both sides draw ids from this one counter, so never lower it. highestPersistedId covers a counter lost to an
	// abrupt exit.
	_nextTabId = std::max({ _nextTabId, s.value(KEY_NEXT_TAB_ID, 1).toULongLong(), highestPersistedId + 1 });
	for (PersistedTab& tab : persistedTabs)
	{
		if (tab.id == 0)
			tab.id = _nextTabId++;
	}

	// v1 restores only the active tab's history (saved under the legacy key).
	const QStringList historyList = s.value(p == Panel::LeftPanel ? KEY_HISTORY_L : KEY_HISTORY_R).toStringList();
	const std::vector<QString> history(historyList.cbegin(), historyList.cend());

	// The side-wide visited-locations log is independent of any tab's lifetime, so it's loaded once here
	// rather than per-tab.
	const QStringList visitedList = s.value(p == Panel::LeftPanel ? KEY_LPANEL_VISITED_LOCATIONS : KEY_RPANEL_VISITED_LOCATIONS).toStringList();
	_visitedLocations[side].addLatest(std::vector<QString>(visitedList.cbegin(), visitedList.cend()));

	auto& tabList = _panels[side];
	for (int i = 0; i < (int)persistedTabs.size(); ++i)
	{
		const PersistedTab& persisted = persistedTabs[(size_t)i];
		CPanel& tab = createTab(p, persisted.id);
		if (i == activeIndex)
			tab.restoreHistory(history);
		// Seed the cursor before the folder is listed: the listing reads it back to position the cursor.
		if (persisted.cursorHash != 0)
			tab.setCurrentItemHashForFolder(persisted.path, persisted.cursorHash, false);
		// The active tab's setPath is deferred below so it's always the most-recently-visited entry in the
		// side-wide visited-locations list on restore, regardless of tab order.
		if (i != activeIndex)
			tab.setPath(persisted.path, refreshCauseOther);
	}
	tabList.activeTab = (size_t)activeIndex;
	tabList.tabs[tabList.activeTab]->setPath(persistedTabs[(size_t)activeIndex].path, refreshCauseOther);
	// The restored tabs are all inactive so far, so this is the only folder listed at startup; the rest are listed
	// when they're first activated.
	tabList.tabs[tabList.activeTab]->setActive(true);
}

void CController::savePanelState(Panel p)
{
	const size_t side = (size_t)p;
	const auto& tabList = _panels[side];
	if (tabList.tabs.empty())
		return;

	QStringList tabRecords;
	for (const auto& tab : tabList.tabs)
	{
		const QString path = tab->currentDirPathPosix();
		tabRecords.push_back(encodeTabRecord(tab->id(), tab->currentItemHashForFolder(path), path));
	}
	const int activeIndex = (int)tabList.activeTab;

	// Dedup: contents-changed also fires on watcher refreshes, where nothing relevant to persistence changed.
	// The records carry the current-item hashes, so a cursor move (e.g. captured at shutdown) isn't swallowed.
	QString signature = QString::number(activeIndex);
	for (const QString& record : tabRecords)
	{
		signature += QChar('\n');
		signature += record;
	}

	if (signature == _lastSavedTabSignature[side])
		return;

	_lastSavedTabSignature[side] = signature;

	QSettings s;
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_TABS : KEY_RPANEL_TABS, tabRecords);
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_ACTIVE_TAB : KEY_RPANEL_ACTIVE_TAB, activeIndex);
	s.setValue(KEY_NEXT_TAB_ID, _nextTabId);

	// Mirror the active tab's path to the legacy key (back-compat + crash recovery).
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_PATH : KEY_RPANEL_PATH, tabList.tabs[tabList.activeTab]->currentDirPathPosix());
}

void CController::saveHistory()
{
	for (const Panel p : { Panel::LeftPanel, Panel::RightPanel })
		saveHistoryList(p);
}

void CController::saveHistoryList(Panel p)
{
	const size_t side = (size_t)p;
	const auto& tabList = _panels[side];
	if (tabList.tabs.empty())
		return;

	QSettings s;
	const CPanel& activeTab = *tabList.tabs[tabList.activeTab];
	const auto& historyDeque = activeTab.history().list();
	s.setValue(p == Panel::LeftPanel ? KEY_HISTORY_L : KEY_HISTORY_R, QStringList(historyDeque.cbegin(), historyDeque.cend()));

	const auto& visitedDeque = _visitedLocations[side].list();
	s.setValue(p == Panel::LeftPanel ? KEY_LPANEL_VISITED_LOCATIONS : KEY_RPANEL_VISITED_LOCATIONS, QStringList(visitedDeque.cbegin(), visitedDeque.cend()));
}

void CController::onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause /*operation*/)
{
	savePanelState(p); // Every tab is persisted, so this deliberately doesn't filter by tabId

	if (_panelContentsSubscriptions.empty() || tabId != activeTabId(p))
		return; // Plugins are shown the tab the user is looking at, not background tabs

	publishPanelContents(p);
}

void CController::onPanelContentsInvalidated(Panel /*p*/, qulonglong /*tabId*/)
{
	// Nothing to persist yet: the cursor hash for the folder being navigated to is only meaningful once its
	// contents are in, and the path is saved by the contents-changed notification that follows.
}

void CController::onCurrentPathChanged(Panel p, const QString& newPath)
{
	auto& visited = _visitedLocations[(size_t)p];

	const size_t sizeBefore = visited.size();
	const bool alreadyLogged = std::find(visited.begin(), visited.end(), newPath) != visited.end();

	visited.addLatest(newPath);

	warnIfVisitedLocationDropped(p, newPath, sizeBefore, alreadyLogged);
}

void CController::warnIfVisitedLocationDropped(Panel p, const QString& newPath, size_t sizeBefore, bool wasAlreadyLogged)
{
	// Tripwire for the "visited folders silently disappear" bug: addLatest must only append newPath (or move it to the
	// newest slot when it was already logged), evicting a single oldest entry only once the maxSize ceiling is reached.
	// Any other resulting size means an entry was dropped.
	const auto& visited = _visitedLocations[(size_t)p];
	const size_t permissibleSize = std::min(wasAlreadyLogged ? sizeBefore : sizeBefore + 1, visited.maxSize());
	if (visited.size() == permissibleSize)
		return;

	// We run inside setPath with the panel mutex held, so a modal here would deadlock against queued panel work
	// draining on the UI thread - surface it via the UI queue, which runs it after setPath has released the lock.
	const size_t actualSize = visited.size();
	_uiQueue.enqueue([p, newPath, sizeBefore, permissibleSize, actualSize] {
		const QString panelSide = p == Panel::LeftPanel ? QSL("left") : QSL("right");
		QMessageBox::critical(nullptr, QSL("Visited-locations history tripwire"),
			QSL("An entry was unexpectedly dropped from the %1 panel's visited-folders history while adding:\n%2\n\n"
				"Size before: %3, expected: %4, actual: %5.\n\nPlease note the steps that led here and report them.")
				.arg(panelSide, newPath).arg(qulonglong(sizeBefore)).arg(qulonglong(permissibleSize)).arg(qulonglong(actualSize)));
	});
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
		const QString lastPathForDrive = QSettings{}.value(p == Panel::LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(QString::fromLatin1(drivePath.toUtf8().toHex())) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(QString::fromLatin1(drivePath.toUtf8().toHex())), drivePath).toString();
		return {setPath(p, toPosixSeparators(lastPathForDrive), refreshCauseOther) == FileOperationResultCode::Ok, drivePath};
	}
}

// Porgram settings have changed
void CController::settingsChanged()
{
	_iconProvider.settingsChanged();
}

void CController::activePanelChanged(Panel p)
{
	assert_and_return_r(p == Panel::LeftPanel || p == Panel::RightPanel, );
	std::unique_lock lock(_pluginAccessMutex);
	if (_panels[(size_t)p].tabs.empty())
		return;

	_activePanel = p;
}

void CController::selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes)
{
	std::unique_lock lock(_pluginAccessMutex);
	if (_panels[(size_t)p].tabs.empty())
		return;

	_selectedItemsHashes[(size_t)p] = selectedItemsHashes;
}

// Navigates specified panel up the directory tree
void CController::navigateUp(Panel p)
{
	panel(p).navigateUp();
	notifyCurrentVolumeChanged(p);
	saveDirectoryForCurrentVolume(p);
}

// Go to the previous location from history, if any
void CController::navigateBack(Panel p)
{
	panel(p).navigateBack();
	saveDirectoryForCurrentVolume(p);
	notifyCurrentVolumeChanged(p);
}

// Go to the next location from history, if any
void CController::navigateForward(Panel p)
{
	panel(p).navigateForward();
	saveDirectoryForCurrentVolume(p);
	notifyCurrentVolumeChanged(p);
}

// Sets the specified path, if possible. Otherwise reverts to the previously set path
FileOperationResultCode CController::setPath(Panel p, const QString &path, FileListRefreshCause operation)
{
	const FileOperationResultCode result = panel(p).setPath(path, operation);
	// The visited-locations log is updated via CPanel's onCurrentPathChanged callback, not here.

	saveDirectoryForCurrentVolume(p);
	notifyCurrentVolumeChanged(p);
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
		setCurrentItemHashForCurrentFolder(activePanelPosition(), newHash, false);
	}

	if (parentDir.exists(name))
		return FileOperationResultCode::TargetAlreadyExists;

	if (!parentDir.mkpath(name))
	{
		// Restore the previous current item in case of failure
		setCurrentItemHashForCurrentFolder(activePanelPosition(), currentItemHash);
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

	const QString newFilePath = parentDir.fullAbsolutePath() % '/' % name;
	if (QFile::exists(newFilePath))
		return FileOperationResultCode::TargetAlreadyExists;

	if (QFile(newFilePath).open(QFile::WriteOnly))
	{
		if (parentDir.fullAbsolutePath() == activePanel().currentDirPathPosix())
			// This is required for the UI to know to set the cursor at the new file
			setCurrentItemHashForCurrentFolder(activePanelPosition(), CFileSystemObject(newFilePath).hash());

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

// Designates a new current item (e. g. a folder is being renamed and we want to keep the cursor on it).
// Applies to whichever folder the given panel is currently showing.
void CController::setCurrentItemHashForCurrentFolder(Panel p, qulonglong newCurrentItemHash, const bool notifyUi)
{
	panel(p).setCurrentItemHashForFolder(panel(p).currentDirPathPosix(), newCurrentItemHash, notifyUi);
}

void CController::copyCurrentItemPathToClipboard()
{
	const auto item = currentItem();
	if (item.isValid())
		QApplication::clipboard()->setText(escapedPath(toNativeSeparators(item.fullAbsolutePath())));
}

const CPanel &CController::panel(Panel p) const
{
	switch (p)
	{
	case Panel::LeftPanel:
	case Panel::RightPanel:
	{
		const TabList& tabList = _panels[(size_t)p];
		assert_r(!tabList.tabs.empty());
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
		assert_r(!tabList.tabs.empty());
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
		assert_unconditional_r("Unknown panel");
		return Panel::LeftPanel;
	}
}

Panel CController::activePanelPosition() const
{
	std::shared_lock lock(_pluginAccessMutex);
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

const CHistoryList<QString>& CController::visitedLocations(Panel p) const
{
	return _visitedLocations[(size_t)p];
}

CThreadPool& CController::threadPool()
{
	return _workerPool;
}

CIconProvider& CController::iconProvider()
{
	return _iconProvider;
}

std::vector<qulonglong> CController::selectedItemsHashes(Panel p) const
{
	if (p == Panel::UnknownPanel)
		return {};

	std::shared_lock lock(_pluginAccessMutex);
	return _selectedItemsHashes[(size_t)p];
}

QString CController::currentFolderPath(Panel p) const
{
	if (p == Panel::UnknownPanel)
		return {};

	std::shared_lock lock(_pluginAccessMutex);
	if (_panels[(size_t)p].tabs.empty())
		return {};

	return panel(p).currentDirPathPosix();
}

CFileSystemObject CController::currentItem(Panel p) const
{
	if (p == Panel::UnknownPanel)
		return {};

	std::shared_lock lock(_pluginAccessMutex);
	if (_panels[(size_t)p].tabs.empty())
		return {};

	return panel(p).currentItem();
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
	return panel(p).currentItemHashForFolder(dir);
}

qulonglong CController::currentItemHash()
{
	return activePanel().currentItemHashForFolder(activePanel().currentDirPathPosix());
}

CFileSystemObject CController::currentItem()
{
	std::shared_lock lock(_pluginAccessMutex);
	if (_activePanel == Panel::UnknownPanel)
		return {};

	return panel(_activePanel).currentItem();
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

void CController::notifyCurrentVolumeChanged(Panel p) noexcept
{
	for (auto& listener: _volumesChangedListeners)
		listener->currentVolumeChanged(p);
}

QString CController::volumePathById(uint64_t id) const
{
	const auto info = _volumeEnumerator.volumeById(id);
	return info ? info->rootObjectInfo.fullAbsolutePath() : QString{};
}

void CController::saveDirectoryForCurrentVolume(Panel p)
{
	const auto currentVolume = currentVolumeInfo(p);
	if (!currentVolume) // A network share, or anything else outside the enumerated volumes: no drive to key by
		return;

	const QString drivePath = currentVolume->rootObjectInfo.fullAbsolutePath();
	QSettings().setValue(p == Panel::LeftPanel ? QString{KEY_LAST_PATH_FOR_DRIVE_L}.arg(QString::fromLatin1(drivePath.toUtf8().toHex())) : QString{KEY_LAST_PATH_FOR_DRIVE_R}.arg(QString::fromLatin1(drivePath.toUtf8().toHex())), panel(p).currentDirPathPosix());
}
