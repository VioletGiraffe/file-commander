#pragma once

// Helpers shared by all CPanel test files: a throwaway directory tree (temptree.h), an event-recording listener, and
// a panel wired to a worker pool the test can step.

#include "cpanel.h"
#include "cfilesystemobject.h"
#include "settings.h"
#include "temptree.h"

#include "qt_helpers.hpp" // operator<< for QString, so Catch2 can print a path in a failure report


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "directory_watcher.hpp" // thin_io
#include "qt_paths.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#elif defined __APPLE__
#include <sys/stat.h> // chflags, UF_HIDDEN
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Returns once the system has reported every change made so far in the temporary folder, which holds every TempTree:
// a watch made afterwards receives none of them. On macOS, FSEvents can deliver a change made shortly before a watch to it.
inline void waitUntilChangesAreReported()
{
	// The marker goes beside the trees: its removal inside one would be one more change to report
	const QString folder = QDir::tempPath();
	const QString markerName = QStringLiteral("file-commander-panel-test.reported");
	const QString marker = folder % '/' % markerName;

	auto watcher = thin_io::directory_watcher::watch(thin_io::native_path(folder).data(), { .recursive = false });
	REQUIRE(watcher);
	REQUIRE(QFile{ marker }.open(QFile::WriteOnly));

	bool reported = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
	while (!reported && std::chrono::steady_clock::now() < deadline)
	{
		const auto pending = watcher->wait_for_events(std::chrono::milliseconds{ 100 });
		REQUIRE(pending);
		if (!*pending)
			continue;

		const auto events = watcher->read_events();
		REQUIRE(events);
		reported = std::ranges::any_of(*events, [&markerName](const thin_io::watch_event& event) { return thin_io::to_qstring(event.path) == markerName; });
	}

	REQUIRE(QFile::remove(marker));
	REQUIRE(reported);
}

[[nodiscard]] inline bool hasCommittedContents(const CPanel& panel)
{
	bool offered = false;
	panel.readCommittedContents([&offered](const QString&, const FileListHashMap&) { offered = true; });
	return offered;
}

[[nodiscard]] inline size_t committedItemCount(const CPanel& panel)
{
	size_t count = 0;
	panel.readCommittedContents([&count](const QString&, const FileListHashMap& contents) { count = contents.size(); });
	return count;
}

// Windows takes the hidden attribute; Linux/FreeBSD honor a leading dot; macOS honors neither for Qt's
// isHidden() (it reads the filesystem UF_HIDDEN flag), so set that explicitly there.
inline bool setFileHidden(const QString& path)
{
#if defined _WIN32
	return ::SetFileAttributesW(reinterpret_cast<const wchar_t*>(path.utf16()), FILE_ATTRIBUTE_HIDDEN) != FALSE;
#elif defined __APPLE__
	return ::chflags(QFile::encodeName(path).constData(), UF_HIDDEN) == 0;
#else
	return QFileInfo{ path }.fileName().startsWith('.');
#endif
}

// The suite shares one settings store and the panel re-reads this setting on every listing, so a test that
// changes it must put it back.
class ShowHiddenFilesSetting
{
public:
	explicit ShowHiddenFilesSetting(bool show) :
		_previousValue{ QSettings{}.value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool() }
	{
		QSettings{}.setValue(KEY_INTERFACE_SHOW_HIDDEN_FILES, show);
	}

	~ShowHiddenFilesSetting()
	{
		QSettings{}.setValue(KEY_INTERFACE_SHOW_HIDDEN_FILES, _previousValue);
	}

	ShowHiddenFilesSetting(const ShowHiddenFilesSetting&) = delete;
	ShowHiddenFilesSetting& operator=(const ShowHiddenFilesSetting&) = delete;

private:
	const bool _previousValue;
};

struct PanelEvent
{
	enum Type { ContentsChanged, ContentsInvalidated, CurrentItemChanged, CurrentPathChanged };

	Type type;
	Panel panel = Panel::UnknownPanel;
	qulonglong tabId = 0;
	FileListRefreshCause cause = refreshCauseOther; // ContentsChanged only
	QString path;                                   // The folder for CurrentItemChanged, the new path for CurrentPathChanged
	qulonglong itemHash = 0;                        // CurrentItemChanged only
};

// Records what the panel reports, in order. Order is the point: the two contents notifications share a queue tag,
// so which of them survives a drain is exactly what the tests are about.
class RecordingListener final : public PanelContentsChangedListener, public CurrentItemChangedListener, public CurrentPathChangedListener
{
public:
	void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) override
	{
		_events.push_back(PanelEvent{ PanelEvent::ContentsChanged, p, tabId, operation, {}, 0 });
	}

	void onPanelContentsInvalidated(Panel p, qulonglong tabId) override
	{
		_events.push_back(PanelEvent{ PanelEvent::ContentsInvalidated, p, tabId, refreshCauseOther, {}, 0 });
	}

	void onCurrentItemChanged(Panel p, qulonglong tabId, const QString& folder, qulonglong currentItemHash) override
	{
		_events.push_back(PanelEvent{ PanelEvent::CurrentItemChanged, p, tabId, refreshCauseOther, folder, currentItemHash });
	}

	void onCurrentPathChanged(Panel p, const QString& newPath) override
	{
		_events.push_back(PanelEvent{ PanelEvent::CurrentPathChanged, p, 0, refreshCauseOther, newPath, 0 });
	}

	[[nodiscard]] const std::vector<PanelEvent>& events() const { return _events; }

	[[nodiscard]] std::vector<PanelEvent::Type> sequence() const
	{
		std::vector<PanelEvent::Type> types;
		types.reserve(_events.size());
		for (const PanelEvent& event : _events)
			types.push_back(event.type);

		return types;
	}

	[[nodiscard]] size_t count(PanelEvent::Type type) const
	{
		return (size_t)std::count_if(_events.begin(), _events.end(), [type](const PanelEvent& event) { return event.type == type; });
	}

	// Fail the test rather than return something arbitrary when the count isn't what the caller assumed.
	[[nodiscard]] const PanelEvent& only(PanelEvent::Type type) const
	{
		REQUIRE(count(type) == 1);
		return *std::find_if(_events.begin(), _events.end(), [type](const PanelEvent& event) { return event.type == type; });
	}

	[[nodiscard]] const PanelEvent& last(PanelEvent::Type type) const
	{
		const auto it = std::find_if(_events.rbegin(), _events.rend(), [type](const PanelEvent& event) { return event.type == type; });
		REQUIRE(it != _events.rend());
		return *it;
	}

	void clear() { _events.clear(); }

private:
	std::vector<PanelEvent> _events;
};

// The pool this suite gives a panel has a single lane, so its worker runs tasks in enqueue order. That buys two
// things no amount of pumping can: parking the worker to hold a file-list update in flight (a slow listing,
// reproduced rather than waited for), and knowing an update has completed while its notification is still
// undelivered.
class WorkerGate
{
public:
	explicit WorkerGate(CThreadPool& pool) noexcept : _pool{ pool } {}

	~WorkerGate()
	{
		open();
		// The parked task refers to this object; it has to be out before the mutex and condition variable die.
		if (_workerWasParked)
			waitForPoolIdle();
	}

	WorkerGate(const WorkerGate&) = delete;
	WorkerGate& operator=(const WorkerGate&) = delete;

	// Parks the worker: everything enqueued from here on waits behind it until open().
	void close()
	{
		{
			std::lock_guard lock{ _mutex };
			if (_closed)
				return;

			_closed = true;
			_workerWasParked = true;
		}

		_pool.enqueue([this] {
			std::unique_lock lock{ _mutex };
			_condition.wait(lock, [this] { return !_closed; });
		});
	}

	void open()
	{
		{
			std::lock_guard lock{ _mutex };
			_closed = false;
		}

		_condition.notify_all();
	}

	// Returns once everything enqueued before the call has finished: on a single lane the probe runs last.
	void waitUntilWorkComplete()
	{
		{
			std::lock_guard lock{ _mutex };
			REQUIRE_FALSE(_closed); // The probe would park behind the gate and never return
		}

		waitForPoolIdle();
	}

private:
	void waitForPoolIdle()
	{
		_pool.enqueueWithFuture([] {}).wait();
	}

private:
	CThreadPool& _pool;
	std::mutex _mutex;
	std::condition_variable _condition;
	bool _closed = false;
	bool _workerWasParked = false;
};

// One CPanel, the pool it works through, and a listener subscribed to everything it reports.
// The panel takes the one pool as both its listing and its worker pool, so the gate steps all of its tasks.
// Tests make their trees before the harness: its panel watches none of the changes that made them.
class PanelHarness
{
public:
	explicit PanelHarness(Panel position = Panel::LeftPanel, qulonglong tabId = 1) :
		_panel{ position, _pool, _pool, tabId }
	{
		waitUntilChangesAreReported();
		_panel.addPanelContentsChangedListener(&_listener);
		_panel.addCurrentItemChangedListener(&_listener);
		_panel.addCurrentPathChangedListener(&_listener);
	}

	[[nodiscard]] CPanel& panel() { return _panel; }
	[[nodiscard]] RecordingListener& listener() { return _listener; }
	[[nodiscard]] WorkerGate& worker() { return _gate; }
	[[nodiscard]] CThreadPool& pool() { return _pool; }

	// One UI-queue drain, exactly what the application's timer does.
	void tick() { _panel.uiThreadTimerTick(); }

	// Lets the update in flight finish, then delivers its notification - never leaving both pending at once, so a
	// test that cares which notification survives the drain keeps control of that.
	void completeUpdate()
	{
		_gate.waitUntilWorkComplete();
		tick();
	}

	// Runs the pipeline to a standstill. More than one round is not padding: an inaccessible path makes the worker
	// post a recovery that moves the panel from inside the tick, starting another update.
	void settle(int maxRounds = 8)
	{
		for (int round = 0; round < maxRounds; ++round)
		{
			const size_t eventCountBefore = _listener.events().size();
			completeUpdate();
			if (_listener.events().size() == eventCountBefore)
				return;
		}

		FAIL("The panel did not reach a steady state");
	}

	// For the one thing the steps above cannot drive: the filesystem watcher, which reports on its own schedule.
	[[nodiscard]] bool pumpUntil(const std::function<bool()>& condition, const std::chrono::milliseconds timeout = std::chrono::seconds{ 10 })
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!condition())
		{
			tick();
			if (std::chrono::steady_clock::now() > deadline)
				return false;

			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}

		return true;
	}

private:
	// Declaration order is load-bearing. Destruction runs bottom-up: the panel retires its own pool tasks first,
	// then the gate releases the worker it may have parked, and only then does the pool join its threads.
	CThreadPool _pool{ 1, "Panel test pool" };
	WorkerGate _gate{ _pool };
	RecordingListener _listener;
	CPanel _panel;
};
