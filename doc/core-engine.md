# Core engine (`file-commander-core/`)

The core is the `core` static library. `file-commander-core.pro` and the included `.pri` files are the source of
truth for its contents. The UI-facing facade is `CController`; implementation lives under `src/`.

## Ownership and panel model

```
CController
 |- two TabLists
 |    `- CPanel per tab
 |         `- current directory, file list, history, watcher
 |- shared panel worker pool
 |- general worker pool and UI execution queue
 |- volume enumerator and favorites
 |- plugin proxy and WCX host
```

`CController::panel(side)` returns that side's active tab. A tab is a `CPanel`; its stable ID is independent of
its vector/QTabBar position. The controller records side listeners and attaches them to tabs created later. See
`ccontroller.{h,cpp}` for the current facade and [tabs.md](tabs.md) for the cross-layer invariants.

Every `CPanel` uses the controller's shared panel pool. Its tasks carry a per-panel tag, and destruction retires
that tag before panel storage disappears. The pool is declared before the tab lists so it outlives every panel.
Inactive tabs release their filesystem watcher and refresh when reactivated.

## File-list publication

An asynchronous operation that can replace a panel's list must preserve the pipeline in `cpanel.{h,cpp}`:

1. Capture a monotonically increasing generation, path, and display mode.
2. Build the complete result in worker-local storage.
3. Publish only through `publishFileListIfCurrent()` after rechecking all three values.
4. Label the retained list with its source path/mode; accessors return no list once the panel has moved to another
   view.

Moving to a view whose list is not ready produces `onPanelContentsInvalidated`; committing the list produces
`onPanelContentsChanged`. Invalidation is display-only: cursor, selection, persistence, and plugin state must wait
for the committed notification. The notifications share a queue tag, so a list completed in the same drain
replaces the invalidation and avoids a visible blank. Both notifications identify their originating tab; listeners
that represent the screen must reject background-tab notifications by ID.

See [threading.md](threading.md) before changing this path.

## Filesystem objects and paths

`CFileSystemObject` (`cfilesystemobject.{h,cpp}`) wraps `QFileInfo` for panel/navigation use. Directory paths are
normalized with a trailing separator before hashing; the deterministic path hash is the application key used by
panels, UI models, selection, and plugins. It is not filesystem object identity: use `resolvedObjectId()` or the
file-operation identity primitives when aliases/links may name the same object.

Links are a sharp edge:

- `isLink()` means a POSIX symlink or Windows name-surrogate link/junction. It deliberately excludes `.lnk`
  shortcuts, which are regular files here; do not substitute `isSymLink()`.
- Qt classification such as `isDir()`, `isFile()`, and size follows the target. The entry's separate link flag is
  therefore required whenever ownership or deletion matters.
- A broken link still exists as an entry and must remain listable and removable.

The file-operation module uses a different path type, `CEntryPath`: absolute, `/`-separated, and without a trailing
separator except for roots. Do not pass assumptions about one representation into the other without using their
conversion/parse boundaries.

## File operations (`src/fileoperations/`)

`fileoperations.pri` lists the current module. Its architecture separates synchronous policy/filesystem logic from
the one threaded wrapper:

```
CFileOperationJob                 worker, pause/cancel/decision handshake, event queue
  `- CTransferExecutor or CDeleteExecutor
       |- COperationExecutionContext   decisions, progress, diagnostics, remembered answers
       |- CDestinationResolver         collision policy
       |- CSourceTreeBuilder           immutable operation-specific manifest
       |- CStagedFileCopy              copy to sibling, transfer metadata, publish by rename
       `- CFileSystemMutator           native inspect/mutate primitives
```

`fileoperationtypes.h` defines requests, issues/decisions, progress, and summaries. `centrypath.h` owns raw path
parsing; `newnamecheck.h` owns validation of names proposed by users. The UI chooses `DestinationIntent` once when
constructing the request; lower layers must not reinterpret it from source count, spelling, or destination state.

Important behavioral boundaries:

1. Executors are synchronous and interact with their environment only through `COperationExecutionContext` and
   the filesystem primitives. `CFileOperationJob` is the only cross-thread owner.
2. Move is rename-first. Only a classified native `CrossDevice` result selects staged copy plus source cleanup;
   after such a result, descendants skip further rename attempts.
3. Staged copy publishes by rename only after data and required metadata are ready. Failure or cancellation before
   publication preserves the old destination. Durability flush is required where publication/cleanup destroys the
   only other copy, not for an ordinary fresh copy.
4. Delete and same-filesystem move operate on link entries. Copy and copy-based move materialize link targets;
   directory-link cycles terminate by native filesystem identity. Content reached through a directory link is
   borrowed and is never removed by move cleanup.
5. Writability remediation applies only to confirmed, non-link regular files. Generic access denied is
   `PermissionDenied`, not `ReadOnly`.
6. Aggregate totals remain absent until every source root has been scanned. `inspectEntry()` distinguishes absence
   from inspection failure; callers must handle both.
7. The committed move segment between publication and source removal has no cancellation checkpoint. Its prompts
   are item-only and do not consult or update remembered decisions.

Inline rename (`inlinerename.{h,cpp}`) is a separate synchronous command with its own replacement matrix, reusing
the name, inspection, identity, and native rename primitives. Test-only deterministic errors/barriers are isolated
in `operationtesthooks.{h,cpp}` and compile out without `FILE_OPERATIONS_TEST_HOOKS`.

## Traversal and watchers

`scanDirectory()` in `directoryscanner.{h,cpp}` is shared by flattened panel display, search, and folder comparison;
file operations and statistics use their own traversals. It can follow directory links, reports whether an item was
reached through one, and breaks cycles using `resolvedObjectId()` rather than path strings. That distinction matters
on Windows, where canonical path text does not reliably resolve junction targets.

Each panel owns a platform watcher selected in `cpanel.h`:

- Windows uses `CFileSystemWatcherWindows`, backed by native change notification handles.
- Other platforms use `CFileSystemWatcherTimerBased`, which compares periodic snapshots. A path generation rejects
  obsolete scans. `CPanel` calls `captureBaselineState()` immediately before its own enumeration so a change made
  after navigation cannot disappear into a later lazy baseline.

Flattened recursive display disarms the single-directory watcher. Inactive tabs also disarm it.

## Other core areas

| Area | Source and boundary |
|------|---------------------|
| Volumes | `diskenumerator/`; periodic enumeration with UI-queued observer delivery. Initial enumeration is synchronous because restored panel paths depend on it. |
| Search | `filesearchengine/`; one interruptible outer thread, with a bounded worker pool only for content searches. The listener must outlive the worker; use `waitForSearchToFinish()` before teardown. |
| Folder/file comparison | `filecomparator/`; synchronous byte/tree primitives plus `CFileComparator` as the single-file threaded wrapper. Pairing policy is explicit in `PairingMode`. |
| Shell integration | `shell/`; platform clipboard, context menus, launching, and supported native deletion backends. |
| Favorites | `favoritelocationslist/`; nested locations persisted under its supplied settings key. |
| Icons | `iconprovider/`; extension guessing is the fast path because it avoids disk access. |
| Statistics/helpers | `filesystemhelpers/`, `filesystemhelperfunctions.{h,cpp}`; recursive statistics, path conversion, and resolved native identity. |

See [qt-ui.md](qt-ui.md) for consumers, [plugins.md](plugins.md) for the plugin surface, and
[persistence.md](persistence.md) for settings ownership.
