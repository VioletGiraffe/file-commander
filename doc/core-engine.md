# Core engine (`file-commander-core/`)

The core is a static library whose UI-facing facade is `CController`. `file-commander-core.pro` and the included
`.pri` files are authoritative for its contents.

## Ownership and panels

```text
CController
 |- two tab lists
 |    `- one CPanel per tab
 |- shared panel worker pool
 |- shared application worker pool and UI execution queue
 |- volumes and favorites
 |- icon provider, with its own retrieval thread and UI execution queue
 `- plugin subscriptions/access gate and WCX host
```

Until shutdown, `CController::panel(side)` returns the active tab. Tabs own their `CPanel` and have stable IDs
independent of display position. Side listeners are attached to existing and newly created tabs. See
[tabs.md](tabs.md) for the cross-layer contract.

`main()` owns the controller and calls `CController::shutdown()` after the event loop and native shell operations
finish, while UI and plugin objects still exist. Shutdown saves state, stops the volume and panel producers, destroys
all tabs, discards queued UI work, and releases callbacks. The shared application pool remains alive so each plugin
proxy can retire its tagged work during plugin-engine destruction; the controller destructor then stops the pool and
asserts the empty-tab postcondition instead of initiating a second shutdown.

`CController::get()` remains bound until destruction, so late shutdown access still reaches a valid controller
object. APIs returning raw panel references are valid only before `shutdown()` destroys the tabs. Plugin-facing
value queries and UI dispatch serialize with teardown and return empty or no-op once their required state is gone.

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
[threading.md](threading.md) before changing this path; [notifications.md](notifications.md) traces one navigation
through it.

## Filesystem objects and paths

`CFileSystemObject` is a value describing one entry for panel and navigation use, read through thin_io once, at
construction: from a path, or from a listing's `directory_entry`. The listings (`listDirectoryForPanel`,
`scanDirectory`, the statistics scan, the polling watcher) enumerate with `listing_detail::full`. Its deterministic
normalized-path hash is the application key used by panels, UI models, selection, and plugins; it is not native
filesystem identity.

Path text grammar (roots, lexical normalization) is `file-operations/paths/pathsyntax.h`; Qt's path functions are not
used for it. A `CFileSystemObject` path must be absolute. Typed text (the address bar, search roots, favorites, the
copy/move destination) goes through `resolveUserPath()` first, which expands environment variables and resolves against
the panel's folder.

Links are entries distinct from their targets:

- `isLink()` is thin_io's `is_link`: POSIX symlinks and Windows name-surrogate reparse points (symlinks, junctions),
  not `.lnk` shortcuts.
- The file/directory classification, size and times follow the target, so ownership-sensitive operations must consult
  the link flag separately.
- Broken links still exist as listable and removable entries, classified by the link's own kind.
- Other Windows reparse points, such as cloud placeholders, are ordinary entries.

## File operations

Copy, move, permanent delete, inline rename, directory scanning and folder comparison live in the
`file-operations` submodule; its `doc/architecture.md` holds their design notes. They build as the static libraries
`fileoperations` and `fileoperationsui`.

## Traversal and watchers

`scanDirectory()` (`file-operations/directoryscan/`) is shared by flattened display, search, and folder comparison. It can
follow directory links and breaks cycles with resolved native identity rather than path text. File operations and
statistics use separate traversals.

Panels watch their folder with `thin_io::directory_watcher` (`src/filesystemwatcher/`):
- A replaced folder is watched again. A lost one (deleted, renamed or moved) is watched again once a folder is back at
  its path; meanwhile the refresh the change triggers moves the panel to an accessible folder.
- Where notifications cannot be set up or may miss changes, the folder is polled with directory snapshots: on
  Linux and macOS a network volume reports only this machine's changes. Windows network shares rely on notifications
  alone, which their server sends for every client. A path generation rejects obsolete polling results.
- The watch is released when its volume is locked for eject, with no refresh and no re-arming: either would touch the
  volume during the eject. The removal itself refreshes the panel: on Windows the main window's device-removal message,
  elsewhere the volume list change.
- Flattened recursive display and inactive tabs hold no watch.

## Other core areas

| Area | Source and boundary |
|------|---------------------|
| Volumes | `diskenumerator/`; periodic enumeration with UI-queued delivery |
| Search | `filesearchengine/`; cancellable traversal with a bounded content-search pool |
| Comparison | `filecomparator/`; a threaded single-file wrapper over `file-operations/foldercomparison/` |
| Shell integration | `shell/`; platform clipboard, context menus, launching, and native deletion |
| Favorites | `favoritelocationslist/`; nested locations persisted under a supplied settings key |
| Icons | `iconprovider/`; extension guessing is the disk-free fast path |
| Statistics/helpers | `filesystemhelpers/`, `filesystemhelperfunctions`; statistics traversal, separators and shell quoting |
