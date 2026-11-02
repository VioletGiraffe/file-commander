#include "paneltesthelpers.h"

#ifdef _WIN32
// FAT and exFAT, for one: there a deleted directory lingers until its last handle closes.
static bool volumeLacksPosixDelete(const QString& directory)
{
	const QString nativeDirectory = QDir::toNativeSeparators(directory);
	const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t*>(nativeDirectory.utf16()), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);
	DWORD flags = 0;
	const bool queried = ::GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) != 0;
	::CloseHandle(handle);
	REQUIRE(queried);
	return (flags & FILE_SUPPORTS_POSIX_UNLINK_RENAME) == 0;
}
#endif

TEST_CASE("CPanel - destroying a panel retires the listing it had queued", "[panel][lifetime]")
{
	TempTree tree;

	CThreadPool pool{ 1, "Panel test pool" };
	WorkerGate gate{ pool };
	RecordingListener listener;

	gate.close();
	{
		CPanel panel{ Panel::LeftPanel, pool, 7 };
		panel.addPanelContentsChangedListener(&listener);
		REQUIRE(panel.setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
		panel.setActive(true);
	} // Retires the queued listing rather than waiting for the parked worker to get to it

	gate.open();
	gate.waitUntilWorkComplete();

	// Nothing ran against the destroyed panel, and its undelivered notifications died with its queue.
	CHECK(listener.count(PanelEvent::ContentsChanged) == 0);
	CHECK(listener.count(PanelEvent::ContentsInvalidated) == 0);
}

TEST_CASE("CPanel - retiring one panel leaves another panel's work alone", "[panel][lifetime]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	CThreadPool pool{ 1, "Panel test pool" };
	WorkerGate gate{ pool };
	RecordingListener listener;
	CPanel survivor{ Panel::RightPanel, pool, 2 };
	survivor.addPanelContentsChangedListener(&listener);

	gate.close();
	{
		CPanel doomed{ Panel::LeftPanel, pool, 1 };
		REQUIRE(doomed.setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
		doomed.setActive(true);
		REQUIRE(survivor.setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
		survivor.setActive(true);
	}

	gate.open();
	gate.waitUntilWorkComplete();
	survivor.uiThreadTimerTick();

	CHECK(listener.count(PanelEvent::ContentsChanged) == 1);
	CHECK(survivor.itemHashExists(hashOf(sub)));
}

TEST_CASE("CPanel - an inactive panel records where it points without listing it", "[panel][watcher]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	// This is how a tab restored from settings sits until the user switches to it: pointed at its folder, but
	// costing nothing to keep there.
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK_FALSE(hasCommittedContents(h.panel()));
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 0);

	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().itemHashExists(hashOf(file)));
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 1);
}

TEST_CASE("CPanel - reactivating a panel picks up what changed while it was inactive", "[panel][watcher]")
{
	TempTree tree;
	tree.makeFile(QStringLiteral("before.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	// An inactive tab gives up its watch handle, so nothing detects this one.
	h.panel().setActive(false);
	const QString added = tree.makeFile(QStringLiteral("while-inactive.txt"));
	h.listener().clear();

	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().itemHashExists(hashOf(added)));
	CHECK(h.listener().count(PanelEvent::ContentsChanged) >= 1);
}

TEST_CASE("CPanel - a change on disk refreshes the folder in view", "[panel][watcher]")
{
	TempTree tree;
	tree.makeFile(QStringLiteral("before.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	const QString added = tree.makeFile(QStringLiteral("after.txt"));

	// The only case in this suite that waits rather than steps: the watcher is a native change handle on Windows
	// and a polling thread elsewhere, so when it reports is not ours to decide. Waiting on the notification rather
	// than on the contents also orders the two - the listing is committed before it is announced.
	CHECK(h.pumpUntil([&h] { return h.listener().count(PanelEvent::ContentsChanged) >= 1; }));
	CHECK(h.panel().itemHashExists(hashOf(added)));
}

// The next two tests keep the folder empty, so no change inside it can trigger the refresh.
TEST_CASE("CPanel - renaming the folder in view moves the panel to its parent", "[panel][watcher]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	REQUIRE(QDir{}.rename(sub, tree.path(QStringLiteral("renamed"))));

	CHECK(h.pumpUntil([&h, &tree] { return h.panel().currentDirPathPosix() == tree.path() + '/'; }));
}

TEST_CASE("CPanel - deleting the folder in view moves the panel to its parent", "[panel][watcher]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	REQUIRE(QDir{}.rmdir(sub));

	CHECK(h.pumpUntil([&h, &tree] { return h.panel().currentDirPathPosix() == tree.path() + '/'; }));
}

TEST_CASE("CPanel - a folder in view replaced under the same name stays watched", "[panel][watcher]")
{
	TempTree tree;
#ifdef _WIN32
	// The panel's watch holds the folder open, so the folder could not be recreated.
	if (volumeLacksPosixDelete(tree.path()))
	{
		WARN("This filesystem keeps a deleted folder while a handle is open: test skipped");
		return;
	}
#endif

	const QString sub = tree.makeDir(QStringLiteral("sub"));
	tree.makeFile(QStringLiteral("sub/old.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	// Replaced before the next tick, so the panel never sees the folder missing
	REQUIRE(QDir{ sub }.removeRecursively());
	tree.makeDir(QStringLiteral("sub"));
	const QString replacementFile = tree.makeFile(QStringLiteral("sub/new.txt"));
	CHECK(h.pumpUntil([&h, &replacementFile] { return h.panel().itemHashExists(hashOf(replacementFile)); }));

	// Proves the watch moved to the replacement
	const QString added = tree.makeFile(QStringLiteral("sub/added.txt"));
	CHECK(h.pumpUntil([&h, &added] { return h.panel().itemHashExists(hashOf(added)); }));
	CHECK(h.panel().currentDirPathPosix() == sub + '/');
}
