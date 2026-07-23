# TODO / backlog

Approved-but-not-started work and consciously deferred items. Current state only; done items get removed.

## Trash support on Linux/FreeBSD

Today `OsShell::deleteItems` exists only on Windows (IFileOperation) and macOS (NSWorkspace), so
`deletionBackendFor` routes both delete variants to the internal permanent-delete job on other platforms,
and the native-shell branch in `CMainWindow::performDeletion` is compiled only under
`#if defined _WIN32 || defined __APPLE__`.

- Implement `OsShell::deleteItems` for Linux/FreeBSD via `QFile::moveToTrash` (freedesktop trash spec).
- `deletionBackendFor` then returns `NativeTrash` for to-trash deletion on those platforms, and the
  call-site guard in `performDeletion` dissolves. `NativeShellPermanent` stays Windows-only (no analogue
  of the shell's own delete UI elsewhere); the internal job keeps covering permanent deletion.
- User-facing behavior change: F8 on Linux starts trashing instead of permanently deleting.
- Until then, consider guarding the `deleteItems` declaration in `cshell.h` with the same platform check,
  so a stray caller fails at compile time instead of link time.

## QoL commands (approved)

### Select / unselect group by mask + select by extension
- TC conventions: Num+ opens a select-by-mask dialog, Num- unselect by mask, Alt+Num+ selects all items
  with the current item's extension.
- Selection lives in the UI layer: `CPanelWidget::_selectionModel` (per-tab `QItemSelectionModel`).
  Iterate sort-model rows, match names against the wildcard mask, `select`/`deselect` rows; skip `[..]`.
- Mask-matching precedent: the filter field machinery (`CFileListFilterDialog`, Ctrl+F) already does
  wildcard matching - reuse its approach.
- Menu home: the Selection menu (currently holds only "Invert selection").

### "Go to file location" in flattened show-all-files mode
- `CMainWindow::showAllFilesFromCurrentFolderAndBelow` shows a flattened list with no way to jump from an
  entry to its real containing folder.
- Sketch: current item's `parentDirPath()` -> `CController::setPath()` on the panel, then put the cursor
  on the item (the per-folder cursor memory may do this for free if primed via `setCurrentItemForFolder`).
- Menu home: Commands menu. Consider enabling only while flattened mode is active.

## Tabs: deferred nice-to-haves

- Double-click empty tab bar area opens a new tab.
- Locked tabs.
- Drag files from the list onto a tab header to copy/move into that tab's folder without switching to it.
- Per-tab history persistence beyond the active tab: only the active tab's back/forward history is
  persisted; other tabs' history resets on restart. Full fix is a list-of-lists with stale-key cleanup.
- Merge the per-side "visited locations" logs (`CController::_visitedLocations`, feeds the path-navigator
  dropdown) into one global list. Has unresolved design wrinkles; not designed yet.
- `CFileListView::_bHeaderAdjustmentRequired` fresh-install gap: the flag is not per-tab and self-clears
  on the first model reset ever, so on a brand-new install switching tabs before resizing a column can
  transiently revert to Qt's default column sizing. Accepted; rare and self-correcting.

Considered and rejected (not worth the complexity at this app's scale) - do not revisit unprompted:
cooperative task-cancellation on tab close; lazy triplet creation for restored-but-unopened tabs.

## File-operation engine: deferred P3 test coverage

Low-priority gaps from the WP11B-1 coverage audit, skipped as minor, academic, or needing a special
environment (root, a mock filesystem, or a specific OS feature). Revisit opportunistically.

- **mutator** (`filesystemmutatortests.cpp`): `EOPNOTSUPP`-when-distinct-from-`ENOTSUP` degradation
  trigger (platform-conditional); `checkSameEntry` `Unknown` verdict when identity is unavailable (needs
  an identity-less mock fs); `isEntryWritableNoFollow` POSIX group/other permission branches (needs
  `chown`/root); `isLinkEntry` Windows non-surrogate reparse point treated as an ordinary entry (needs a
  OneDrive-style environment); `readCopyableDirectoryTimes` missing-`last_write` guard.
- **staged copy** (`stagedfilecopytests.cpp`): POSIX special permission bits setuid/setgid/sticky (mode
  `07777`, needs root); moved-from-session destructor performs no cleanup (verify no double
  `RemoveStaging`); explicit-`abort()` one-shot arrival count == 1.
- **execution context** (`coperationexecutioncontext.cpp`): scope downgrade when
  `remainingMatchingScopeAllowed == false` in copy/delete - the move committed-cleanup segment already
  exercises the path; only worth a dedicated context unit test with a mock decision provider.
