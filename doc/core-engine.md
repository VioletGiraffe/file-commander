# Core engine (`file-commander-core/`)

UI-agnostic static lib (`TARGET = core`). Depends on Qt `core widgets gui` (gui/widgets only for
`QFileIconProvider` and the plugin interface) and submodules `cpputils`, `qtutils`. Public headers:
`include/` (only `settings.h`). All implementation under `src/`.

`config.pri` sets the build: C++23 (`/std:c++latest`, `strict_c++`, `c++2b`), `staticlib`,
`DEFINES += PLUGIN_MODULE`, SSE4.1 on non-ARM non-Windows, MSVC `/W4` + `/permissive-` + `/Zc:__cplusplus`.

## Object graph

```
CController (singleton, CController::get())
 |- std::array<TabList,2> _panels            one TabList per side
 |    TabList { vector<unique_ptr<CPanel>> tabs; size_t activeTab }
 |        CPanel (one per tab) -- current dir, file list, history, watcher
 |            CFileSystemObject (current dir + each listed item)
 |- CWorkerThreadPool _panelWorkerPool       SHARED by every CPanel (declared before _panels)
 |- CPluginProxy _pluginProxy                 API surface handed to plugins
 |- CWcxPluginHost _wcxHost                   TC .wcx archive plugins (Windows; stub elsewhere)
 |- CVolumeEnumerator _volumeEnumerator       background drive enumeration
 |- CFavoriteLocations _favoriteLocations
 |- CWorkerThreadPool _workerThreadPool       general (separate from the panel pool)
 |- CExecutionQueue _uiQueue                  UI-thread task queue (drained on timer)
```

## CController (`src/ccontroller.{h,cpp}`)

Central singleton, `CController::get()`. The single facade the UI calls; owns everything stateful.
Implements `IVolumeListObserver`, `PanelContentsChangedListener`, `CurrentPathChangedListener` (it listens
to its own panels purely to drive persistence and the visited-locations log).

- **Panels/tabs:** `std::array<TabList,2> _panels`. `panel(Panel p)` returns the **active tab's** `CPanel`
  for side `p` — this indirection is why most call sites never had to learn about tabs. `otherPanel`,
  `activePanel`, `activePanelPosition`, `activePanelChanged(p)`.
- **Tab API (ID-based; ids are `qulonglong`, `_nextTabId` starts at 1, 0 = invalid):** `addTab(p,path,activate)`,
  `closeTab(p,id)` (never removes the last tab), `setActiveTab`, `moveTabPosition`, `tabCount`, `activeTabId`,
  `tabIds` (display order), `tabPath(p,id)`, `tabName(p,id)`. Private helpers `createTab`, `attachListenersToTab`,
  `switchActiveTab`, `tabIndexById`. See [tabs.md](tabs.md).
- **Navigation/ops (delegate to the active CPanel):** `setPath`, `navigateUp/Back/Forward`,
  `refreshPanelContents`, `itemActivated(hash,p)` (open file / cd into folder / mount archive),
  `createFolder`, `createFile`, `openTerminal`, `displayDirSize`, `showAllFilesFromCurrentFolderAndBelow`,
  `setCursorPositionForCurrentFolder`, `copyCurrentItemPathToClipboard`, `switchToVolume`.
- **Item access by hash:** `itemHashExists`, `itemByHash`, `items(hashes)`, `itemPath`, `currentItem`,
  `currentItemHash`, `currentItemHashForFolder`.
- **Volumes:** `volumes()`, `currentVolumeInfo`, `volumeInfoForObject`, `volumeInfoById`; observer fan-out
  via `setVolumesChangedListener`. Per-side last-path-per-drive remembered (`saveDirectoryForCurrentVolume`).
- **Visited locations:** `visitedLocations(p)` — per-side, **tab-independent** `CHistoryList<QString>` log of
  visited folders (survives tab close, unlike a tab's own back/forward history). Fed by `onCurrentPathChanged`.
  Powers the path navigator's quick-revisit dropdown.
- **Threading:** `execOnWorkerThread` (general `_workerThreadPool`), `execOnUiThread(task,tag)` (`_uiQueue`),
  `uiThreadTimerTick()` drains the queue + pumps panels. See [threading.md](threading.md).
- **Persistence (centralized here — CPanel no longer touches settings):** `restorePanelState`/`savePanelState`
  (deduped via `_lastSavedTabSignature`; migrates legacy single-path keys), `saveHistoryList` (shutdown only).
  Dtor saves state on graceful shutdown. See [persistence.md](persistence.md).
- Records per-side listener lists `_panelContentsListeners` / `_cursorPositionListeners` so tabs created
  later also receive them (re-attached by `attachListenersToTab`).

## CPanel (`src/cpanel.{h,cpp}`) — one per tab

**Not a QObject** (deliberately). Represents one tab's view of one directory.

- Ctor `CPanel(Panel position, CWorkerThreadPool& pool, qulonglong id)` — uses the controller's **shared**
  pool, not its own. `id()` is a stable per-tab identity assigned by the controller. `_taskTag =
  reinterpret_cast<uint64_t>(this)` tags this panel's async tasks; the dtor calls `pool.retire(_taskTag)`
  so no in-flight task touches freed memory.
- State: `_currentDirObject` (FSO), `_items` (`FileListHashMap`), `_history` (`CHistoryList<QString>`),
  `_cursorPosForFolder` (`segmented_map<QString,qulonglong>` — remembers cursor item per folder),
  `_watcher` (`FileSystemWatcher`), `_currentDisplayMode` (Normal / AllObjects).
- `setActive(bool)`: an **inactive tab releases its filesystem watch handle**; activating re-arms the watch
  and refreshes (folder changes weren't observed while inactive). Resource-saving for many tabs.
- Navigation: `setPath`, `navigateUp/Back/Forward`, `history()`, `goToItem`, `showAllFilesFromCurrentFolderAndBelow`.
- Dir info: `currentDirObject`, `currentDirPathNative/Posix`, `currentDirName`.
- File list: `refreshFileList(cause)`, `list()`, `itemHashExists/itemByHash/itemPathByHash/itemHashes`,
  `displayDirSize(hash)` (async size calc, then data-change notification).
- Cursor memory: `setCurrentItemForFolder`, `currentItemForFolder`.
- Notifies three observer lists (`CallbackCaller<...>`): `PanelContentsChangedListener`,
  `CursorPositionListener`, `CurrentPathChangedListener`. `restoreHistory(vector)` seeds history on restore.
- Sync: `mutable std::recursive_mutex _fileListAndCurrentDirMutex` guards the file list + current dir
  (touched from the worker pool and the UI). `_uiThreadQueue` (`CExecutionQueue`) marshals back to UI.

## CFileSystemObject (`src/cfilesystemobject.{h,cpp}`)

Wrapper around `QFileInfo` for one file/dir/bundle. Carries a `CFileSystemObjectProperties` value
(`size, hash, completeBaseName, extension, fullName, fullPath, type, exists, isLink`). `type()` is
`UnknownType/Directory/File/Bundle`.

- Rich predicates: `isFile/isDir/isBundle/isEmptyDir/isCdUp/isExecutable/isReadable/isWriteable/isHidden/
  isLink/isSymLink/isNetworkObject`, `symLinkTarget`, `rootFileSystemId` (which volume the entry is on).
- **Links are the sharp edge.** Qt's *classification* calls (`isDir/isFile/exists/size`) transparently
  follow a symlink/junction to its target and that cannot be turned off — a dir link classifies as
  `Directory`, which is why `type` deliberately stays `Directory` for one (navigation works). To ask "is
  this *itself* a link?" use `isLink()` = `QFileInfo::isSymbolicLink() || isJunction()`, computed once in
  `refreshInfo()` and stored. **Not `isSymLink()`** — the legacy Qt call also reports `true` for Windows
  `.lnk` shortcuts, which are regular files here. `exists = isLink || QFileInfo::exists()` so a broken link
  still counts as existing, and a broken link gets `type = File` so it stays listed and deletable. (The
  file-operation engine's own no-follow link handling lives with that engine, not here.)
- `hash()` is the identity used everywhere. `setDirSize` is a documented hack to stash a computed dir size.
- Times (`creationTime`/`modificationTime`) are lazily cached (`mutable`, sentinel `invalid_time`).
- **Test seam:** under `#define CFILESYSTEMOBJECT_TEST`, `QFileInfo`/`QDir` are macro-swapped for
  `QFileInfo_Test`/`QDir_Test` mocks (header `#define` at top, `#undef` at bottom). Free function
  `pathHierarchy(path)` lives here only because a CFSO test covers it and it needs the same QFileInfo include.

## File operations (`src/fileoperations/`)

Copy, move, and permanent delete. A layered stack of **synchronous, single-purpose pieces** with one thin
threading wrapper at the top — so all policy and filesystem logic is unit-testable without threads, and only
one small class is ever concurrent.

```
CFileOperationJob      worker thread + event queue  (the only concurrent piece)
  drives one of:
    CTransferExecutor  recursive copy / move
    CDeleteExecutor    recursive permanent delete
      through COperationExecutionContext   the executor's entire environment: cancel/pause
                                            checkpoints, decision prompts, progress, diagnostics,
                                            remembered answers, the accumulating summary
      using:
        CDestinationResolver   one collision   -> an issue + a user decision
        CSourceTreeBuilder     one source root -> an immutable manifest tree
        CStagedFileCopy        one file: copy to a temp sibling, transfer metadata, publish by rename
        CFileSystemMutator     stateless inspect / rename / remove / create / writability / same-object / dir-times
```

- **Typed vocabulary (`fileoperationtypes.h`, `centrypath.h`).** A request is a `TransferRequest` or a
  `PermanentDeleteRequest` (variant `FileOperationRequest`). A problem is an `OperationIssue` — an `IssueKind`
  (file replacement, directory merge, type mismatch, read-only source removal, unsupported entry, or generic
  action-failed) plus `EntrySnapshot`s and an optional structured `FailureDetails`. The answer is a `Decision`
  (`DecisionAction` × `DecisionScope`). Native errors are classified into a `FileErrorCategory` at the point of
  failure and never travel as raw codes above the primitive layer. `CEntryPath` is the one normalized absolute
  path (single `/` separator, no trailing separator except roots) used throughout the module.
- **The executors are synchronous, with no threads, UI, or ambient I/O of their own.** Every side effect goes
  through `CFileSystemMutator` / `CStagedFileCopy`; every contact with the outside world goes through
  `COperationExecutionContext` callbacks (`checkpoint()` for pause/cancel, `resolveDecision()` for prompts,
  progress publish, warning/failure recording). The context also owns the **remembered-decision table**
  ("...to all", keyed by `IssueKind`, stored only for rememberable actions) and accumulates the
  `OperationSummary`.
- **`CFileOperationJob` is the only cross-thread owner** — one worker thread (`CInterruptableThread`), one
  mutex, one condition variable, one event queue. It binds the context callbacks to that machinery and exposes
  `OperationEvent` (`ProgressSnapshot | DecisionRequest | OperationSummary`), which the UI drains via
  `processEvents`. See [threading.md](threading.md).
- **Move is rename-first.** A native rename is attempted at every owned root/subtree boundary; only a
  classified `CrossDevice` result (EXDEV / `ERROR_NOT_SAME_DEVICE`) selects the staged-copy fallback with its
  committed source-cleanup segment. Once a boundary classifies cross-device, the whole subtree skips rename.
- **Staged copy = atomic publish.** A file is streamed to a uniquely created, preallocated temp sibling;
  metadata (times, permissions) is applied there; then it atomically replaces the destination by rename — so
  an overwrite replaces only the selected pathname, leaving symlink targets and other hard-link names for the
  destination untouched. Any pre-publication failure or cancellation discards the temp file and preserves the
  old destination; a copy-based move removes its source only after publication succeeds.

Inline rename (single-item rename from the panel, `inlinerename.{h,cpp}`) reuses the same mutator primitives
and the same case-only / same-object rules, without the batch machinery. Test-only fault injection lives in
`operationtesthooks.{h,cpp}` (compiled only under `FILE_OPERATIONS_TEST_HOOKS`): it forces a native error, or
pauses a worker, at a named point so tests can exercise failure and race paths deterministically.

**Sharp edges worth knowing:**

- **No-follow link discipline.** Inspect / remove / make-writable on a link entry act on the *link itself*,
  never its target; copy and copy-based move *materialize* a link target's contents, while delete and
  same-drive move act on the link entry. A broken link stays a listed, removable entry. Link cycles during
  materialization are broken by filesystem **identity** (device+inode / volume-serial+file-index), not path
  strings. A link is never routed through writability remediation.
- **Read-only is a narrow category.** `FileErrorCategory::ReadOnly` is set only from an unambiguous native
  meaning (POSIX `EROFS`; Windows media write-protect / read-only attribute); generic access-denied stays
  `PermissionDenied` and must never be treated as read-only. Only a confirmed non-link regular-file source
  removal turns a fresh read-only reading into the read-only-source-removal policy question.
- **Totals stay absent until every root is scanned** — a partial aggregate must never render as an exact
  total. `inspectEntry` likewise distinguishes *absent* (an empty optional) from *error* (an `unexpected`);
  both paths must be handled by callers.
- **The committed move-cleanup segment (publish -> remove source) takes no cancellation checkpoint**, and
  every prompt inside it is item-only — it neither consults nor updates the remembered-decision table.

## Directory traversal — `scanDirectory` (`src/directoryscanner.{h,cpp}`)

Free function: `scanDirectory(root, observer, abort = atomic<bool>{false}, followDirLinks = true)`.
Recursively walks `root`, invoking `observer(fso, reachedThroughLink)` per item, abortable via the atomic
flag. The shared enumeration behind **size calculation, "show all files below", and search** — the
file-operation engine does *not* use it (it scans through its own `CSourceTreeBuilder`). (Default arg binds a
const ref to a temporary `atomic` — valid for the whole call incl. recursion; see [oddities.md](oddities.md).)

- **`followDirLinks`** — when `false`, a dir link is reported as an item but not descended into (for a caller
  that must not recurse into a link's target).
- **`reachedThroughLink`** — `true` for items found by traversing a link, letting a caller treat linked
  content differently from owned content.
- **Cycle guard.** Following links can loop (a link to its own ancestor, or two links pointing at each other).
  The scanner keeps the chain of directories currently on the recursion stack and refuses to descend into a
  link whose target is one of them. **Identity is compared via `resolvedObjectId` (device+inode /
  volume-serial+file-index), not path strings** — `QFileInfo::canonicalFilePath()` does not resolve NTFS
  junction targets, so an earlier path-prefix version passed on POSIX but let junction cycles through on
  Windows. Only links pay for the identity lookups; plain trees never reach this branch.

## Filesystem watching (`src/filesystemwatcher/`)

`using FileSystemWatcher =` platform pick:
- **Windows:** `CFileSystemWatcherWindows` — `QAbstractNativeEventFilter`, native `HANDLE`-based change
  notifications. `setPathToWatch` / poll `changesDetected()`; both thread-safe (`_mtx`).
- **Else:** `CFileSystemWatcherTimerBased` — periodic re-scan on a `CPeriodicExecutionThread`, diffing a
  `std::set<FileSystemInfoWrapper>` (name + size). Each path assignment advances a generation; obsolete scans
  are discarded and the first committed scan for the new generation establishes a baseline without reporting
  a change. Same `setPathToWatch` / `changesDetected` poll contract.

Each `CPanel` owns one and polls it on the UI timer tick.

## Volumes (`src/diskenumerator/`)

`CVolumeEnumerator` (a `QObject`): background drive/volume enumeration on a `CPeriodicExecutionThread`
(1 s interval). `startEnumeratorThread()` is a separate second-phase init. `volumes()` / `volumeById` /
`updateSynchronously()`. Observers (`IVolumeListObserver::volumesChanged(bool significant)`) notified via a
`CExecutionQueue` (UI thread). `_mutexForDrives` is **recursive** because `updateSynchronously()` can call
`enumerateVolumes()` while a getter already holds the lock. Platform impls:
`cvolumeenumerator_impl_{win,mac,linux,freebsd}.cpp`; `VolumeInfo` value type in `volumeinfo.hpp`.

## Other core subsystems

- **`OsShell` namespace (`src/shell/cshell.{h,cpp}`, `cshell_mac.mm`)** — native shell integration:
  context menu for items (`openShellContextMenuForObjects`), clipboard cut/copy/paste of files,
  recycle-bin delete (`deleteItems(moveToTrash)`) + recycle-bin context menu, tooltips, run executables
  (`runExe(... asAdmin)` on Windows), `shellExecutable()`, `executeShellCommand`, `isInPath`. Heavy
  Win32/COM on Windows; `.mm` Cocoa on macOS. `main.cpp` does `CO_INIT_HELPER(COINIT_APARTMENTTHREADED)`.
- **`CIconProvider` (`src/iconprovider/`)** — static `iconForFilesystemObject(fso, guessByExtension)`;
  the `guessByExtension=true` path avoids disk access (fast). Pimpl (`CIconProviderImpl`); reacts to
  `settingsChanged()`.
- **`CFavoriteLocations` (`src/favoritelocationslist/`)** — nested favorites tree (`CLocationsCollection`
  with `subLocations`), persisted under its own settings key (`KEY_FAVORITES`). Owned by the controller.
- **`CFileSearchEngine` (`src/filesearchengine/`)** — name + content search on a `CInterruptableThread`.
  `search(filters, caseSens, where, contents, ..., listener)`; reports via `FileSearchListener`
  (`itemScanned`/`matchFound`/`searchFinished(status, itemsScanned, msElapsed)`); `stopSearching`. Name-only
  searches use only that outer thread. Content searches lazily add 1-8 workers, bound outstanding file tasks
  to twice the worker count, and check cancellation between 4 KiB chunks. Backs the Find Files dialog.
- **`CFileComparator` (`src/filecomparator/`)** — binary/byte comparison engine; used by the file-comparison
  plugin and covered by `filecomparator_test`.
- **`filestatistics` / `filesystemhelpers` / `filesystemhelperfunctions`** — recursive size/count stats,
  path helpers, misc fs utilities. `resolvedObjectId(path)` (in `filesystemhelperfunctions`) returns
  the unique identity of the entry a path resolves to (`{device, inode}` on POSIX, `{volume serial, file
  index}` via `GetFileInformationByHandle` on Windows) — the correct primitive for "same file/dir?" tests,
  used by both link cycle guards. `filestatistics`' parallel BFS carries the same guard as a visited-target set.

## Key core data structures

- `FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, IdentityHash>`
  — a panel's current file list, keyed by item hash. `segmented_map` keeps element addresses stable across
  growth; `IdentityHash` because the key is already a good hash.
- `CHistoryList<QString>` (from qtutils) — bounded back/forward navigation history; used both per-tab and
  for the per-side visited-locations log.

See [qt-ui.md](qt-ui.md) for how the UI consumes all this, [plugins.md](plugins.md) for the plugin surface.
