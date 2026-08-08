# Threading and concurrency

Qt's main thread owns the UI. Filesystem enumeration, operations, search, and polling run elsewhere and return
through execution queues, listener callbacks, or typed operation events. Threading primitives live in the
`cpputils` submodule; read their headers before relying on exact queue or pool semantics.

## Execution map

| Owner | Mechanism | Work |
|-------|-----------|------|
| `CController` | shared panel worker pool | All `CPanel` tasks across sides and tabs |
| `CController` | UI execution queue | Plugin UI dispatch and work deferred past panel locks |
| each `CPanel` | UI execution queue | Panel result publication and observer delivery |
| each `CFileOperationJob` | interruptible thread | One copy, move, or delete request |
| `CShellOperationRunner` | interruptible thread per operation | Blocking native shell calls |
| `CFileSearchEngine` | interruptible thread plus bounded pool | Traversal and content matching |
| volume enumerator and polling watcher | periodic threads | Device and directory change polling |
| Windows watcher | native handle plus Qt event filter | Directory change notification |

`CMainWindow` periodically drains every tab's panel queue and the controller UI queue. Tagged queue entries can
replace older pending entries with the same tag; work queued during a drain waits for the next tick.

## Panel lifetime and publication

Every pool task that captures a `CPanel` carries its nonzero task tag. Panel destruction aborts its scan and retires
the tag, removing queued work and waiting for already-started work from that panel without waiting for other tabs.
Controller declaration order keeps the pool alive until all panels are gone.

Every asynchronous list replacement captures generation, path, and display mode, builds locally, and commits
through `publishFileListIfCurrent()`. Invalidation only withdraws stale display data; committed notification is the
point where cursor, selection, persistence, and plugin state may change. Screen-facing callbacks filter by active
tab ID. See [core-engine.md](core-engine.md) for the complete publication boundary.

## Lock and teardown boundaries

- `CPanel::_fileListAndCurrentDirMutex` protects directory/list state. Do not enter the polling watcher's mutex while
  holding it.
- The polling watcher's baseline and path generation are protected by the watcher mutex across both poll-thread and
  panel-worker access.
- `CVolumeEnumerator` deliberately uses a recursive mutex because synchronous enumeration can re-enter getters.
- `CFileSearchEngine` retains a raw listener; its owner must wait for the search to finish before destruction.
- `CShellOperationRunner` is owned outside the main window and drains UI events while waiting at shutdown. This
  preserves the native owner window and avoids deadlocking shell work that still marshals through the UI thread.

Structural lifetime constraints belong beside the relevant member declarations; preserve those comments when
changing declaration order.

Application shutdown is an explicit quiescence boundary. `main()` calls `CController::shutdown()` while the main
window and plugin modules are still alive. The controller disables proxy callbacks, stops and joins its producers,
discards queued UI work, then destroys the panels; ordinary destruction only verifies that this happened.

## File-operation handshake

`CFileOperationJob` owns one worker and a mutex/condition-variable state machine for pause, cancellation, decisions,
and the event queue. The worker emits progress, decision requests, and one summary; the UI drains and dispatches
them after releasing the mutex because presenting a decision can enter a nested event loop.

Progress may coalesce, while decisions and the final summary preserve ordering. State mutations used by wait
predicates occur under the same mutex before notification. Cancellation releases every wait and invalidates an
undrained decision. Job destruction cancels, wakes, and joins; owning UI objects must outlive that sequence.

## Mandatory review after concurrency changes

Perform a separate invariant pass after reviewing the code and diff:

1. Name every shared field and the lock or thread that owns it.
2. Trace each flag and counter through success, cancellation, exception, and early return.
3. Pair every wait with a guaranteed state mutation and notification under the correct mutex.
4. Verify producers cannot outlive queues, callbacks, listeners, or captured owners.
5. Compare concurrent behavior and accounting with the serial equivalent.
6. For panel work, recheck task tagging, generation/path/mode validation, notification meaning, and tab-ID filtering.
