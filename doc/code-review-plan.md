# Code-review map

Use this map to keep broad reviews tractable and to examine high-risk dependencies before their consumers. For a
diff-driven review, select the affected areas and always apply the cross-cutting checklist.

## Review areas

| Order | Area | Primary sources | Main risks |
|-------|------|-----------------|------------|
| 1 | File operations | `file-commander-core/src/fileoperations/` | Data loss, link handling, staged publication, cancellation and cleanup |
| 2 | Concurrency and panel lifetime | `ccontroller`, `cpanel`, watchers, search, operation jobs | Untagged work, stale publication, teardown races, queue ordering |
| 3 | Controller, tabs, and persistence | `ccontroller`, `cpanel`, `CPanelWidget` | Stable-ID mapping, last-tab invariant, active-tab state, save/restore alignment |
| 4 | File-list UI | `qt-app/src/panel/` | Active-model invariant, selection, drag/drop destinations, rename and sorting |
| 5 | Filesystem and platform integration | filesystem helpers, shell, watchers, volume enumeration | Native error handling, identity, links, cross-platform parity |
| 6 | Plugins and standalone subsystems | plugin engine/interface, shipped plugins, comparator, search, icons, favorites | Dynamic-library lifetime, callback ownership, subsystem-specific correctness |
| 7 | Application shell | `CMainWindow`, dialogs, settings, tools | Command routing, dialog/job lifetime, settings round trips, action wiring |

Review tests and build/installer files alongside the product area they exercise. Submodules and vendored code are
separate repositories or third-party inputs and require separate review scope.

## Cross-cutting checklist

1. Establish ownership and destruction order across raw pointers, callbacks, dialogs, tabs, queues, and plugin
   libraries.
2. For concurrency, name each shared field and its synchronization or owning thread; trace cancellation, waits,
   notifications, and early exits. Apply the dedicated checklist in [threading.md](threading.md).
3. Verify failures propagate without leaving partial state, and that platform-specific errors are not classified
   more narrowly than the available evidence supports.
4. Preserve path-hash, stable-tab-ID, link-entry, and active-model invariants.
5. Compare platform branches and record intentional gaps rather than assuming parity.
6. Check the relevant tests against the behavior under review, including failure and cancellation paths.
7. Review the final diff separately from the implementation pass for missed call sites, stale comments, and
   unnecessary complexity.
