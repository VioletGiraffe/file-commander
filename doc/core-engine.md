# Core engine (`file-commander-core/`)

The core is a static library built with QtCore alone, whose UI-facing facade is `CController`.
`file-commander-core.pro` is authoritative for its contents. The plugin interfaces and engine are a separate
library: see [build-ci-deps.md](build-ci-deps.md).

## Ownership and panels

```text
CController
 |- two tab lists
 |    `- one CPanel per tab
 |- shared panel listing pool
 |- shared application worker pool and UI execution queue
 |- volumes and favorites
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

Each `CPanel` holds its tab's sorting, name filter and selection. A front end shows a copy of `committedListing()`
through a `SortedFileList` with those criteria. The selection holds only
items of the committed listing, never `[..]` and never an item the filter hides: a committed listing keeps the
selected items it still has at the same path, and a filter change deselects what it hides. Commands take their
targets from `selectedItems()` or `selectedItemsOrCurrentItem()`, in display order.

Panels share the controller's two pools: file list updates run in the listing pool, which runs nothing else, so
other work cannot delay them; folder size calculations run in the application pool. Every task that captures a
panel carries that panel's task tag; destruction retires the tag in both pools before panel storage disappears. The
pools' declaration order makes them outlive all panels. Inactive tabs release their filesystem watcher and refresh
when reactivated.

A panel shows its folder, the flattened tree below it, or search results sent to it. Search results have no watch.
Both front ends call `CController::refreshUnwatchedViews()` when a file operation ends: it lists again the views their
watch does not keep current (`CPanel::watchKeepsViewCurrent()`).

## File-list publication

An asynchronous operation that can replace a panel list must:

1. Capture the current generation, path, and display mode.
2. Build the complete result in worker-local storage.
3. Publish only through `publishFileListIfCurrent()`, which rechecks all three values.

An operation that changes items of the committed listing in place (a calculated folder size, a flattened view's
watched changes) does so under the list
mutex, only while the listing belongs to the current view, and records the items through `commitItemChangesLocked()`.
The front ends then apply those items alone; a full refill compares every row. Past 700 items changed since the last
notification, the next one is a full refill instead.

Measured on a 2-core i3-2310M with the `filelist_test` timing cases, sorted by name, rows spread over folders:

| | 100k rows | 500k rows |
|---|---|---|
| Refill for one changed file, GUI | 40-61 ms | 190-300 ms |
| One change applied in place, GUI | 9-16 ms, mostly the view's relayout | 43-54 ms, mostly the view's relayout |
| Each further change in place, GUI | about 0.07 ms | about 0.36 ms, about 0.28 ms of it in the Qt model and view |
| GUI break-even against a refill | about 700 changes | about 700 changes |
| Terminal UI break-even | past 20k changes | between 20k and 62k changes |

The cap follows the GUI. An insertion or removal shifts the display order, a `std::vector` of row indices: that makes a
change's cost grow with the rows in both front ends.

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

UI-thread code that only compares paths must not construct a `CFileSystemObject`: construction stats the path, which
blocks for the network timeout on an unreachable share. `resolveUserPath()` returns the same normalized form, so the
texts compare directly.

Links are entries distinct from their targets:

- `isLink()` is thin_io's `is_link`: POSIX symlinks and Windows name-surrogate reparse points (symlinks, junctions),
  not `.lnk` shortcuts.
- The file/directory classification, size and times follow the target, so ownership-sensitive operations must consult
  the link flag separately.
- Broken links still exist as listable and removable entries, classified by the link's own kind.
- Other Windows reparse points, such as cloud placeholders, are ordinary entries.

## File operations

Copy, move, permanent delete, inline and batch rename, directory scanning and folder comparison live in the
`file-operations` submodule; its `doc/architecture.md` holds their design notes. They build as the static libraries
`fileoperations` and `fileoperationsui`.

`fileoperationlaunch` turns a front end's selected paths and destination text into a typed request and chooses the
deletion backend. It decides `DestinationIntent` before path parsing, because a trailing separator is user intent
even when the directory does not yet exist. A front end that may run over SSH, the terminal UI, never deletes through
the desktop shell: it trashes through Qt and deletes permanently with the internal job. `CController::moveToTrash`
runs Qt's trash, or macOS's shell trash, off the UI thread and reports the items it refused. On Linux and FreeBSD,
Empty Trash deletes what `OsShell::trashContents` lists, through the internal job.

The panel commands a front end runs on user input (new folder or file, rename, go to a typed path, switch volume) are
`CController` methods. They check the input, act, and return the engine's `UserMessage` to show; creation and rename
also put the cursor on the entry. The front end collects the input, shows the message, and asks rename's replacement
question.

The batch rename dialogs of both front ends render `CBatchRenameSession` (`src/batchrename/`), a model with no UI: the preview with each
item's state, the renaming and its undo, and the options saved between uses.

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
- A flattened view watches its tree recursively and applies the changed paths in place, rereading each on disk:
  [threading.md](threading.md) has the ordering rules. A lost event, a polled change or a re-armed watch lists the tree
  again. The poll covers only the top folder. In-place updates start at most every 250 ms: the GUI's relayout after
  inserting or removing rows takes about 43 ms at 500k rows.
- Arming the tree's watch walks the whole tree to find its folder links, and on Linux to place an inotify watch on
  each folder. The flattened listing arms it on the listing pool before it scans, and the UI thread takes it over.
  A re-arm after the folder is lost or replaced, and the re-walk after an inotify queue overflow, run on the UI thread.
- The watch follows folder links as the listing does. A link that appears lists the tree again: an in-place scan of
  it would have no folders above it to break a cycle with.
- Removing the items under a path a folder may have left compares every row's path, under the list mutex. It is skipped
  when the path held a listed file. inotify cannot tell a removed file from a removed link, so on Linux every removed
  path the listing left out costs a sweep. Measured on the same i3-2310M with the `filelist_test` timing case:

  | Folders swept | 100k rows | 500k rows |
  |---|---|---|
  | One holding no listed item | 6.8 ms | 34 ms |
  | One holding 100 files | 9.6 ms | 48 ms |
  | 100 holding 100 files each | 28 ms | 128 ms |
- Search results and inactive tabs hold no watch.

## Other core areas

| Area | Source and boundary |
|------|---------------------|
| Volumes | `diskenumerator/`; periodic enumeration with UI-queued delivery |
| File list rules | `filelist/`; the display name, sort order, name filter and totals of a panel's rows; `SortedFileList` keeps the rows in display order for a UI's list |
| Search | `filesearchengine/`; cancellable traversal with a bounded content-search pool |
| Comparison | `filecomparator/`; threaded wrappers over `file-operations/foldercomparison/`, for one file and for two folders |
| Shell integration | `shell/`; platform clipboard, Explorer's context menus, launching, native deletion and the freedesktop.org trash. `xdgapplications` finds and starts the freedesktop.org applications registered for file types, for Linux and FreeBSD |
| Favorites | `favoritelocationslist/`; locations, nested groups and separators, persisted under a supplied settings key. The format of earlier versions is read from a second key while the first is absent |
| Statistics/helpers | `filesystemhelpers/`, `filesystemhelperfunctions`; statistics traversal, separators and shell quoting |
