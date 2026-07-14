# Threading & concurrency model

The UI runs on Qt's main thread. Anything potentially slow (filesystem I/O, enumeration, copy/move/delete,
search, volume polling) runs off-thread and marshals results back to the UI thread. The threading
primitives (`CWorkerThreadPool`, `CExecutionQueue`, `CPeriodicExecutionThread`, `CInterruptableThread`)
come from the **qtutils**/**cpputils** submodules — described here by role, not internals.

## Threads / executors in play

| Mechanism | Owner | Purpose |
|-----------|-------|---------|
| `CWorkerThreadPool _panelWorkerPool` | `CController` (shared) | 1-4 workers for all panel tasks across **every tab on both sides**. Injected into each `CPanel`. Declared before `_panels` so it outlives them. |
| `CWorkerThreadPool _workerThreadPool` | `CController` | General `execOnWorkerThread` tasks not tied to a panel. Destroyed before `_uiQueue`, which its tasks can post to. |
| lazy `CWorkerThreadPool` | each content search with eligible files | 1-8 content workers, with at most two outstanding file tasks per worker. Name-only searches do not construct it. |
| `CExecutionQueue _uiQueue` | `CController` | Tasks to run on the UI thread; drained on the UI timer tick. Declared before `_workerThreadPool` so it outlives that producer. |
| `CExecutionQueue _uiThreadQueue` | each `CPanel` | Per-panel UI marshaling (`execOnUiThread`). |
| `std::thread _thread` | each `COperationPerformer` | One copy/move/delete batch; blocks on a condition variable for user decisions. |
| `CInterruptableThread` | `CFileSearchEngine` | One file search; `stopSearching()` interrupts it. |
| `CPeriodicExecutionThread` | `CVolumeEnumerator` (1 s) and the **non-Windows** `CFileSystemWatcherTimerBased` | Periodic background polling. |
| native HANDLE + `QAbstractNativeEventFilter` | `CFileSystemWatcherWindows` | Windows change notifications (no extra thread; rides the Qt native event filter). |

## UI-thread marshaling pattern

1. Off-thread code finishes work and enqueues a closure (`execOnUiThread`) or a typed file-operation event.
2. `CMainWindow`'s `QTimer _uiThreadTimer` fires -> `CMainWindow::uiThreadTimerTick()` ->
   `CController::uiThreadTimerTick()` -> drains `_uiQueue`, pumps each `CPanel::uiThreadTimerTick()` (which
   polls its watcher and drains its `_uiThreadQueue`), and pumps volume notifications.
3. Closures run on the UI thread; widgets update.

`execOnUiThread(task, tag)` carries an optional `tag` so queued tasks can be coalesced/cancelled by tag.

Content-search cancellation is checked while waiting for bounded pool capacity and between 4 KiB chunks of
each mapped file. Search completion always drains the bounded pool: at most 16 outstanding tasks exist, and
canceled tasks return promptly. A name-only search uses just its outer `CInterruptableThread`; a content
search adds up to eight content workers only after finding its first eligible file.

## Lifetime safety: task tags + retire

Each `CPanel` computes `_taskTag = reinterpret_cast<uint64_t>(this)` and tags every task it posts to the
**shared** pool with it. The `CPanel` dtor calls `pool.retire(_taskTag)`, guaranteeing no task that captured
`this` runs after the panel is destroyed. **Critical for tabs:** closing a tab destroys its `CPanel` while
the shared pool may hold its in-flight tasks — `retire` removes that tag's queued tasks and waits only for
that tag's popped/running tasks, without waiting for work owned by other tabs. Preserve this whenever you add
async work in `CPanel`.

Every asynchronous operation that replaces a panel's file list also carries that panel's monotonically
increasing file-list generation, path, and display mode. Workers build a complete local map and publish it
only through the guarded commit funnel when the request is still current; obsolete results and recovery
callbacks are discarded. The retained list is labeled with its source path and display mode, and accessors
hide it as soon as the panel moves to a different view. Preserve this for every new list-producing operation.

## Locks

- `CPanel::_fileListAndCurrentDirMutex` — `recursive_mutex` guarding the current directory, file-list
  generation, committed list, and its source-view metadata across the UI and panel-pool threads.
- `CVolumeEnumerator::_mutexForDrives` — `recursive_mutex`; `updateSynchronously()` can enumerate while a
  getter already holds it.
- `COperationPerformer`: `_waitForResponseMutex` + `_waitForResponseCondition` (worker blocks for the user's
  conflict decision); paused/working/done state uses atomics, and cancellation is a shared atomic flag so a
  buffered halt event can validate it after the performer has finished.
- `CFileOperationObserver`: `_eventMutex` guards typed events replayed on `processEvents()`. Progress and
  current-file updates coalesce into the latest state between halt/finish ordering barriers, bounding routine
  update backlog while preserving state-before-barrier order. Events are dispatched with the mutex released.
- Both filesystem watchers: an internal mutex makes `setPathToWatch`/`changesDetected` thread-safe. The
  timer-based watcher associates each scan with a path generation, discards obsolete scans, and treats the
  first committed scan of every generation as a silent baseline.

## File-operation handshake (worker <-> UI)

```
COperationPerformer thread            UI thread (progress dialog)
---------------------------           ---------------------------------
copy/move/delete loop
  hit conflict -> onProcessHalted  -> buffered; replayed unless cancellation has since won
  wait on condition variable            user picks -> userResponse(reason, resp, newName)
  <- wakes, applies resp                (urProceedWithAll/urSkipAll remembered in _globalResponses)
  onProgressChanged (periodic)     -> progress bar / speed / ETA
  onProcessFinished                -> dialog closes
```

`togglePause`/`cancel` flip atomics the worker checks at safe points. Cancellation also releases a paused
worker, and every filesystem-mutation loop re-checks cancellation immediately after its pause boundary.
Halt events re-check the shared cancellation flag at UI dispatch time, including after `processEvents()`
has moved them to its local batch, so cancellation cannot be followed by a stale conflict prompt. Multiple
operations can run at once; `CMainWindow` cascades their dialogs (`_activeFileOperationDialogs`,
`nextBackgroundDialogPosition`).

## Reminder (project rule)

Always do a dedicated, thorough review pass after any threading/concurrency change — never skip it
(project memory `feedback-threading-review`). The tag/retire and active/inactive-watch logic are the
easiest places to introduce a use-after-free or a missed refresh.
