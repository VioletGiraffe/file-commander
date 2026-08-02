# Oddities, smells & open questions

Observations noted while documenting — **not** a code review. Confidence labeled; verify before acting.
Nothing here is known to break the app at runtime except where stated.

## Architectural sharp edges (by design, but easy to break)

- **Per-tab models read the *active* tab.** `CFileListModel` / `CFileListSortFilterProxyModel` resolve all
  data through `CController::panel(side)` = active tab, not the tab they were created for. Correct only
  because the architecture guarantees just the active triplet is attached to the view. Any change that
  queries a non-active tab's model (e.g. background prefetch, a second view) will silently read the wrong
  tab's data. See [tabs.md](tabs.md). (Design invariant — flagged as a maintenance hazard, not a current bug.)
- **Shared worker pool + tab lifetime.** Closing a tab destroys its `CPanel` while the shared
  `_panelWorkerPool` may still hold its tasks. Safety depends entirely on `_taskTag` + `pool.retire(_taskTag)`
  in `~CPanel`. Any new async work added in `CPanel` must be tagged, or it can outlive the panel and touch
  freed memory. See [threading.md](threading.md).

## Looks alarming, is actually fine (don't re-investigate)

- **`scanDirectory(..., const std::atomic<bool>& abort = std::atomic<bool>{false})`** binds a const
  reference to a temporary default. Verified safe: the temporary lives for the whole top-level call
  (full-expression), and the recursion in `directoryscanner.cpp` passes the same reference down, so it never
  dangles. The only hazard would be a future refactor that *stores* the reference beyond the call.
- **`CFileSystemObject::isWriteable()` returns false for non-existing files** (documented in the header).
  Gotcha for callers that test writability of a not-yet-created path — check the parent dir instead.
- **The file-operation executors are fully synchronous and block freely** — including blocking the worker on
  the user's decision (`resolveDecision`) and on pause. Intended: they run on `CFileOperationJob`'s worker
  thread, never on the UI thread, and reach the UI only by queuing events the dialog drains. Don't "fix" them
  into async/non-blocking form. See [threading.md](threading.md).

## Two-phase init patterns to be aware of

- `CVolumeEnumerator()` constructs, then `startEnumeratorThread()` must be called separately to begin polling.
- `CPluginProxy` is constructed with a UI-thread executor function; menu-entry creation needs
  `setToolMenuEntryCreatorImplementation` wired by the UI first.
