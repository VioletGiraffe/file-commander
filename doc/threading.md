# Threading and concurrency

Qt's main thread owns the UI. Filesystem enumeration, operations, search, and periodic polling run elsewhere and
return through `CExecutionQueue`, listener callbacks, or typed operation events. The reusable primitives live in the
`cpputils` submodule; read their headers before depending on exact queue/pool semantics.

## Execution map

| Owner | Mechanism | Work |
|-------|-----------|------|
| `CController` | shared `CWorkerThreadPool _panelWorkerPool` (1-4 workers) | All `CPanel` tasks across both sides and every tab. |
| `CController` | `CExecutionQueue _uiQueue` | Plugin UI-thread calls, and deferring work past a held panel lock. |
| `CShellOperationRunner` | one `CInterruptableThread` per operation | Blocking OS shell calls (delete, clipboard paste). |
| each `CPanel` | `CExecutionQueue _uiThreadQueue` | Panel results and observer delivery on the UI thread. |
| each `CFileOperationJob` | `CInterruptableThread` | One copy/move/delete request around synchronous executors. |
| `CFileSearchEngine` | `CInterruptableThread`; bounded pool for content search | Search traversal and file-content matching. |
| `CVolumeEnumerator` | `CPeriodicExecutionThread` | Volume polling. |
| non-Windows panel watcher | `CPeriodicExecutionThread` | Directory snapshot polling. |
| Windows panel watcher | native handle + Qt event filter | Directory change notifications without another thread. |

`CMainWindow`'s UI timer calls `CController::uiThreadTimerTick()`, which drains every tab's panel queue (including
inactive tabs with an older in-flight result) and the controller UI queue. An execution-queue tag replaces an older
pending task with the same tag. An `execAll` drain executes only the tasks present when the drain began; work queued
by those tasks waits for the next tick.

## Panel lifetime and publication invariants

Every pool task that captures a `CPanel` must carry its nonzero `_taskTag`. The panel destructor first asks its
background scan to abort, then calls `CWorkerThreadPool::retire(tag)`. Retirement removes queued work of that owner
and waits for already-popped work of the same tag, without waiting for unrelated tabs. The shared pool must outlive
all panels; `CController` member order enforces that.

Every asynchronous list replacement carries the current generation, path, and display mode, builds a local map,
and commits only through `publishFileListIfCurrent()`. Accessors hide a retained list as soon as it no longer belongs
to the current view. Preserve this funnel rather than adding a second publication path or incrementally mutating
`_items` from a worker.

List replacement has two notifications:

- `onPanelContentsInvalidated`: the old list no longer describes the view; display may blank, but derived state must
  not be updated.
- `onPanelContentsChanged`: the new list is committed; cursor, selection, persistence, and plugin state may update.

They share a queue tag, allowing a fast completion to replace the pending invalidation. Both carry a tab ID, as does
the asynchronous current-item callback. A screen-facing listener must compare it with `activeTabId()`; folder path
is not an identity because duplicate tabs may show the same folder.

## Locks and ownership

- `CPanel::_fileListAndCurrentDirMutex` guards the directory, list generation, committed list, and source-view
  metadata. Do not hold it while entering the polling watcher's mutex.
- `CVolumeEnumerator::_mutexForDrives` is recursive because synchronous enumeration can re-enter a getter.
- The polling watcher's baseline is written by its poll thread and by the panel worker's synchronous
  `captureBaselineState()`, always under the watcher mutex and tagged with a path generation.
- `CShellOperationRunner` is owned by `main()`, which joins every operation between `exec()` returning and the main
  window's destruction. That ordering is what keeps the window - whose handle those operations gave the shell as
  their dialog owner - alive for their whole duration.
  Its thread list is touched only on the UI thread, by `run()` and by the queued reaping, so it needs no lock.

Declaration order that encodes lifetime is documented beside the relevant members. Preserve or update those
comments when changing ownership.

## File-operation handshake

`CFileOperationJob` owns one worker, one mutex covering control state and the event queue, and one condition variable
for pause, cancellation, and decisions. Its event type is
`ProgressSnapshot | DecisionRequest | OperationSummary`.

```
worker                               UI dialog
executor reaches issue
  -> queue DecisionRequest           drain, present modal prompt
  -> wait                            submitDecision(), wake worker
publish progress                     render progress
finish -> queue summary              render outcome / dispose
```

Progress events coalesce; decisions and the summary are ordering barriers. Manifest scanning deliberately publishes
each discovered entry without another throttle: coalescing bounds the queue and the dialog observes only at its timer
cadence, while an unmeasured producer-side mutex overwrite does not justify another timing policy. `processEvents()`
swaps the queue under the mutex and dispatches after unlocking because a modal prompt can enter a nested event loop.
Wait predicates and their state mutations use the same mutex, then the mutator notifies. Cancellation releases
pause/decision waits, invalidates an undrained decision, and wins over a simultaneously submitted answer. The first
valid decision submission makes its request immediately unanswerable; duplicate submissions are rejected even before
the worker consumes the answer. A cancellation requested before `start()` is remembered across the thread wrapper's
flag reset.

The job destructor cancels, wakes, and joins. UI objects owning jobs/listeners must preserve that ordering; see
`CFileOperationDialog` and its member comments.

## Search

Name-only search uses the outer `CInterruptableThread`. Content search lazily creates 1-8 workers and bounds
outstanding file tasks to twice the worker count; cancellation is checked while waiting for capacity and between
file chunks. `CFileSearchEngine` retains a raw listener pointer, so the listener owner must call
`waitForSearchToFinish()` before destruction.

## Mandatory review after concurrency changes

Perform a separate invariant pass after reading the code and diff:

1. Name every shared field and the lock/thread that owns it.
2. Trace each flag/counter through success, cancellation, exception, and early-return paths.
3. Pair every wait with a guaranteed state mutation and notification under the correct mutex.
4. Verify producer objects cannot outlive their queues, callbacks, listeners, or captured owners.
5. Compare parallel behavior and accounting with the serial equivalent.
6. For panel work, recheck task tagging, generation/path/mode validation, notification meaning, and tab ID filtering.
