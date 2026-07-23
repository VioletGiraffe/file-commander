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
| `CInterruptableThread` | each `CFileOperationJob` | One copy/move/delete batch; runs the synchronous executor and blocks on a condition variable for user decisions. |
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
- `CFileOperationJob`: a single mutex covers both the control state and the event queue, and one condition
  variable wakes the worker for resume, cancellation, and decision submission. Cancellation is the wrapper
  thread's own flag (there is no duplicate job-side boolean); every wait predicate is evaluated under the
  mutex and every waker mutates under it before notifying, so a lost wakeup is unrepresentable. The queued
  events (`ProgressSnapshot | DecisionRequest | OperationSummary`) are swapped out under the mutex and then
  dispatched with it released, because a modal decision prompt may enter a nested event loop and call back
  in. Repeated progress snapshots coalesce to the latest; `DecisionRequest` and `OperationSummary` are
  ordering barriers that never coalesce across.
- Both filesystem watchers: an internal mutex makes `setPathToWatch`/`changesDetected` thread-safe. The
  timer-based watcher associates each scan with a path generation, discards obsolete scans, and treats the
  first committed scan of every generation as a silent baseline.

## File-operation handshake (worker <-> UI)

```
CFileOperationJob worker thread          UI thread (progress dialog)
-------------------------------          ---------------------------------
synchronous executor runs
  hit an issue -> resolveDecision   -> DecisionRequest queued (a barrier)
  wait on condition variable            dialog drains queue on a timer, presents a modal prompt
  <- wakes, applies Decision            submitDecision(decision) (an "...all" answer is remembered
                                        in the context, keyed by IssueKind)
  publish ProgressSnapshot (coalesced) -> progress bar / speed / ETA
  finish -> OperationSummary queued -> dialog renders the summary, then disposes itself
```

`setPaused`/`cancel` set state under the job mutex and notify the condition variable, which the worker
re-checks at every `checkpoint()`. Cancellation also releases a paused worker, drops any undrained
`DecisionRequest` (it is now unanswerable), and wins even over a decision that arrived in the same wakeup.
A `cancel()` before `start()` is remembered and applied at start, past the wrapper's cancellation-flag reset.
Multiple operations can run at once; `CMainWindow` cascades their dialogs (`_activeFileOperationDialogs`,
`nextBackgroundDialogPosition`), and each dialog removes itself from that list when it finishes or is
dismissed (see [qt-ui.md](qt-ui.md)).

## Reminder (project rule)

Always do a dedicated, thorough review pass after any threading/concurrency change — never skip it
(project memory `feedback-threading-review`). The tag/retire and active/inactive-watch logic are the
easiest places to introduce a use-after-free or a missed refresh.
