# Core engine (`file-commander-core/`)

The core is a static library whose UI-facing facade is `CController`. `file-commander-core.pro` and the included
`.pri` files are authoritative for its contents.

## Ownership and panels

```text
CController
 |- two tab lists
 |    `- one CPanel per tab
 |- shared panel worker pool
 |- general worker pool and UI execution queue
 |- volumes and favorites
 `- plugin proxy and WCX host
```

Until shutdown, `CController::panel(side)` returns the active tab. Tabs own their `CPanel` and have stable IDs
independent of display position. Side listeners are attached to existing and newly created tabs. See
[tabs.md](tabs.md) for the cross-layer contract.

`main()` owns the controller and calls `CController::shutdown()` after the event loop and native shell operations
finish, while UI and plugin objects still exist. Shutdown saves state, stops asynchronous producers, releases
callbacks, and destroys all tabs. The destructor asserts the empty-tab postcondition instead of initiating a second
shutdown.

`CController::get()` remains bound until destruction, so late shutdown access still reaches a valid controller
object. Panel-dependent APIs are valid only before `shutdown()` destroys the tabs.

Panels share the controller's panel worker pool. Every task that captures a panel carries that panel's task tag;
destruction retires the tag before panel storage disappears. The pool's declaration order makes it outlive all
panels. Inactive tabs release their filesystem watcher and refresh when reactivated.

## File-list publication

An asynchronous operation that can replace a panel list must:

1. Capture the current generation, path, and display mode.
2. Build the complete result in worker-local storage.
3. Publish only through `publishFileListIfCurrent()`, which rechecks all three values.

Accessors hide a retained list as soon as it no longer describes the current view. Invalidation means only that the
old list is no longer displayable; cursor, selection, persistence, and plugin state update only after a committed
contents notification. Screen-facing listeners filter all notifications by stable tab ID. See
[threading.md](threading.md) before changing this path.

## Filesystem objects and paths

`CFileSystemObject` wraps `QFileInfo` for panel and navigation use. Its deterministic normalized-path hash is the
application key used by panels, UI models, selection, and plugins; it is not native filesystem identity.

Links are entries distinct from their targets:

- `isLink()` includes POSIX symlinks and Windows name-surrogate links or junctions, but not `.lnk` shortcuts.
- Qt's file/directory classification follows the target, so ownership-sensitive operations must consult the link
  flag separately.
- Broken links still exist as listable and removable entries.
- Windows reparse points without the name-surrogate bit remain ordinary entries.

The file-operation engine uses `CEntryPath`: an absolute `/`-separated path without a trailing separator except at
roots. Keep conversions at the type's boundaries. The parser rejects embedded NUL and user-supplied Win32 namespace
prefixes; native-path prefixing belongs to the native bridge. See
[planned-refactors/native-paths.md](planned-refactors/native-paths.md) for the remaining POSIX filename limitation.

## File operations (`src/fileoperations/`)

The module separates synchronous policy and filesystem mechanics from its threaded UI wrapper:

```text
CFileOperationJob
  `- CTransferExecutor or CDeleteExecutor
       |- COperationExecutionContext
       |- CDestinationResolver
       |- CSourceTreeBuilder
       |- CStagedFileCopy
       `- CFileSystemMutator
```

`fileoperationtypes.h` defines requests, issues, decisions, progress, and summaries. `centrypath.h` owns operation
paths; `newnamecheck.h` validates user-proposed names. The UI chooses `DestinationIntent` once when building a
request; lower layers do not reinterpret it.

Important boundaries:

1. Executors are synchronous. `CFileOperationJob` is the cross-thread owner and the sole pause, cancel, decision,
   and event-queue boundary.
2. Move attempts native rename first. Only a classified cross-device result selects staged copy and owned-source
   cleanup.
3. Staged copy publishes by rename after contents and required metadata are ready. Cancellation is honored before
   publication; publication and committed source cleanup are not interruptible.
4. Delete and same-filesystem move act on link entries. Copy materializes link targets, detects active traversal
   cycles, and never removes content merely borrowed through a directory link.
5. Manifest scanning builds the complete operation-specific tree before aggregate totals become available. Fixed
   depth and node limits bound hostile or cyclic inputs.
6. Native error classification is conservative. Narrow a platform error only when the operation's local context
   proves the meaning; unknown errors remain ordinary I/O failures.
7. Namespace decisions are path-bound. External processes can replace a source or destination between inspection,
   authorization, and mutation; eliminating that limitation would require stable identities plus handle-bound or
   conditional native operations, not another path recheck.

Exclusive publication degrades to a check followed by rename when the filesystem lacks an exclusive-rename
primitive. This preserves compatibility but admits a narrow external race. Publication also relies on normal
filesystem namespace persistence rather than promising power-loss transactional durability.

Inline rename is a separate synchronous command that reuses name, inspection, identity, and native rename
primitives. Test-only fault injection lives in `operationtesthooks` and compiles out of production builds.

## Traversal and watchers

`scanDirectory()` is shared by flattened display, search, and folder comparison. It can follow directory links and
breaks cycles with resolved native identity rather than path text. File operations and statistics use separate
traversals.

Windows panels use native change notifications; other platforms compare periodic directory snapshots. A path
generation rejects obsolete polling results. Flattened recursive display and inactive tabs do not hold a
single-directory watch.

## Other core areas

| Area | Source and boundary |
|------|---------------------|
| Volumes | `diskenumerator/`; periodic enumeration with UI-queued delivery |
| Search | `filesearchengine/`; cancellable traversal with a bounded content-search pool |
| Comparison | `filecomparator/`; synchronous primitives plus a threaded single-file wrapper |
| Shell integration | `shell/`; platform clipboard, context menus, launching, and native deletion |
| Favorites | `favoritelocationslist/`; nested locations persisted under a supplied settings key |
| Icons | `iconprovider/`; extension guessing is the disk-free fast path |
| Statistics/helpers | `filesystemhelpers/`, `filesystemhelperfunctions`; traversal and native identity utilities |
