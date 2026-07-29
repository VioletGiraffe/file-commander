# Core engine (`file-commander-core/`)

The core is the `core` static library. `file-commander-core.pro` and the included `.pri` files are the source of
truth for its contents. The UI-facing facade is `CController`; implementation lives under `src/`.

## Ownership and panel model

```
CController
 |- two TabLists
 |    `- CPanel per tab
 |         `- current directory, file list, history, watcher
 |- shared panel worker pool
 |- general worker pool and UI execution queue
 |- volume enumerator and favorites
 |- plugin proxy and WCX host
```

`CController::panel(side)` returns that side's active tab. A tab is a `CPanel`; its stable ID is independent of
its vector/QTabBar position. The controller records side listeners and attaches them to tabs created later. See
`ccontroller.{h,cpp}` for the current facade and [tabs.md](tabs.md) for the cross-layer invariants.

Every `CPanel` uses the controller's shared panel pool. Its tasks carry a per-panel tag, and destruction retires
that tag before panel storage disappears. The pool is declared before the tab lists so it outlives every panel.
Inactive tabs release their filesystem watcher and refresh when reactivated.

## File-list publication

An asynchronous operation that can replace a panel's list must preserve the pipeline in `cpanel.{h,cpp}`:

1. Capture a monotonically increasing generation, path, and display mode.
2. Build the complete result in worker-local storage.
3. Publish only through `publishFileListIfCurrent()` after rechecking all three values.
4. Label the retained list with its source path/mode; accessors return no list once the panel has moved to another
   view.

Moving to a view whose list is not ready produces `onPanelContentsInvalidated`; committing the list produces
`onPanelContentsChanged`. Invalidation is display-only: cursor, selection, persistence, and plugin state must wait
for the committed notification. The notifications share a queue tag, so a list completed in the same drain
replaces the invalidation and avoids a visible blank. Both notifications identify their originating tab; listeners
that represent the screen must reject background-tab notifications by ID.

See [threading.md](threading.md) before changing this path.

## Filesystem objects and paths

`CFileSystemObject` (`cfilesystemobject.{h,cpp}`) wraps `QFileInfo` for panel/navigation use. Directory paths are
normalized with a trailing separator before hashing; the deterministic path hash is the application key used by
panels, UI models, selection, and plugins. It is not filesystem object identity: use `resolvedObjectId()` or the
file-operation identity primitives when aliases/links may name the same object.

Links are a sharp edge:

- `isLink()` means a POSIX symlink or Windows name-surrogate link/junction. It deliberately excludes `.lnk`
  shortcuts, which are regular files here; do not substitute `isSymLink()`.
- Qt classification such as `isDir()`, `isFile()`, and size follows the target. The entry's separate link flag is
  therefore required whenever ownership or deletion matters.
- A broken link still exists as an entry and must remain listable and removable.

On Windows, `FILE_ATTRIBUTE_REPARSE_POINT` alone does not make an entry a link. Both directory enumeration and
fresh inspection preserve the reparse tag, and only a tag carrying the Windows name-surrogate bit is classified as
a link. Cloud-provider placeholders and other non-surrogate reparse points remain ordinary files or directories:
copying reads their logical contents and may let the provider hydrate them, while move and delete address the entry
itself. File Commander does not currently expose or preserve cloud-placeholder state as a separate feature.

The file-operation module uses a different path type, `CEntryPath`: absolute, `/`-separated, and without a trailing
separator except for roots. Do not pass assumptions about one representation into the other without using their
conversion/parse boundaries. Its parser rejects embedded NUL and user-supplied Win32 namespace prefixes; the native
bridge owns extended-path prefixing. User-proposed names are separately checked for native-forbidden syntax before
they can reach a filesystem mutation.

## File operations (`src/fileoperations/`)

`fileoperations.pri` lists the current module. Its architecture separates synchronous policy/filesystem logic from
the one threaded wrapper:

```
CFileOperationJob                 worker, pause/cancel/decision handshake, event queue
  `- CTransferExecutor or CDeleteExecutor
       |- COperationExecutionContext   decisions, progress, diagnostics, remembered answers
       |- CDestinationResolver         collision policy
       |- CSourceTreeBuilder           immutable operation-specific manifest
       |- CStagedFileCopy              copy to sibling, transfer metadata, publish by rename
       `- CFileSystemMutator           native inspect/mutate primitives
```

`fileoperationtypes.h` defines requests, issues/decisions, progress, and summaries. `centrypath.h` owns raw path
parsing; `newnamecheck.h` owns validation of names proposed by users. The UI chooses `DestinationIntent` once when
constructing the request; lower layers must not reinterpret it from source count, spelling, or destination state.
`OperationEntryKind::Unknown` is reserved for failure diagnostics where inspection established no kind; it never
enters traversal, collision resolution, or mutation.

Important behavioral boundaries:

1. Executors are synchronous and interact with their environment only through `COperationExecutionContext` and
   the filesystem primitives. `CFileOperationJob` is the only cross-thread owner.
2. Move is rename-first. Only a classified native `CrossDevice` result selects staged copy plus source cleanup;
   after such a result, descendants skip further rename attempts. On POSIX filesystems served by a native exclusive
   rename primitive, case respelling does not assume platform-wide case sensitivity: after an exclusive self-collision
   it moves the source to a unique sibling, publishes to the requested spelling exclusively, and restores the original
   spelling exclusively if publication fails. A rollback failure leaves the source at the reported temporary path
   rather than overwriting a raced-in occupant. This sequence is deliberately non-atomic; a process crash between its
   renames can leave that temporary entry behind. A wholesale native rename is unscanned and accounts as one completed
   item and zero transferred bytes; a materializing fallback reports the detailed manifest and bytes it actually
   processes. These counters describe observed execution work, not an execution-path-independent subtree cardinality.
3. Staged copy publishes by rename only after data and required metadata are ready. A final cancellation checkpoint
   separates completed staging from commit; once commit begins, publication and any owned-source cleanup are not
   interruptible. Failure or cancellation before publication preserves the old destination. Durability flush is
   required where publication/cleanup destroys the only other copy, not for an ordinary fresh copy. If preparation
   and staging cleanup both fail, operation policy follows the preparation failure and cleanup is reported separately
   as a warning.
4. Delete and same-filesystem move operate on link entries. Copy and copy-based move materialize link targets;
   directory-link cycles normally terminate by an active-branch traversal identity combining the underlying entry
   with its mounted view. This distinction lets Linux traverse the same inode once through each bind-mounted view,
   whose descendant mount topology may differ, while a return to the same view terminates the cycle. Platforms
   without a distinct mount-view identity retain object-only detection. When a filesystem exposes no stable entry
   identity, materialization still traverses the link and the fixed depth/node limits terminate any cycle before
   execution. Content reached through a directory link is borrowed and is never removed by move cleanup. Transfer
   same-entry checks remain object-based and do not follow links: a link and its target are distinct entries, while
   hard-link aliases remain the same entry.
5. Writability remediation applies only to confirmed, non-link regular files. Generic access denied is
   `PermissionDenied`, not `ReadOnly`. On POSIX, a file's own write permission does not govern `unlink()` or
   `rename()`; the preflight is nevertheless an intentional confirmation policy for write-protected source
   files, not a prediction that removal will fail. Its traditional owner/group/other mode check uses the effective
   UID, effective GID, and supplementary groups with POSIX class precedence. Make-writable authorization adds the
   owner-write bit only at the mutation point; copy-based move defers it until destination publication. A failed
   writability query falls through to the real operation and its ordinary error policy.
6. Aggregate totals remain absent until every source root has been scanned. A descendant that disappears after
   enumeration is omitted when its inspection or directory listing reports `NotFound`; the selected root and every
   non-`NotFound` scan error retain their operation-level failure handling. Processing rechecks cancellation after
   each synchronous directory listing and every 64 listed children; the listing call itself remains one blocking
   filesystem operation.
7. The committed move segment between publication and source removal has no cancellation checkpoint. Its prompts
   are item-only and do not consult or update remembered decisions.
8. Before a root file is staged or any root is renamed, the executor silently creates an absent destination-parent
   chain. Directory materialization provides the same behavior while creating the selected directory itself.
   Existing non-directory parents and genuine inspection or creation failures use
   `ActionFailed(CreateDestinationDirectory)` policy; the mere absence of one or more parents never prompts.
9. Before constructing a source child path, manifest scanning verifies that its native name converts to Qt path
   text without information loss, while allowing Darwin's deliberate Unicode normalization. Failure aborts the
   complete manifest with an `Unsupported` source-inspection diagnostic; it is never treated as a vanished child
   or allowed to alias a different native entry.
10. Manifest construction admits at most 300 root-to-leaf nodes (the selected root is level 1) and 2,000,000
    visited nodes. Crossing either fixed safety limit aborts the complete manifest with an `Unsupported`
    source-inspection diagnostic. The depth limit bounds the builder and all recursive manifest consumers; the
    node limit also bounds repeated traversal of directory-link graphs that do not form an active-branch cycle.
    At the current 64-bit layouts, two million `SourceNode` objects alone occupy roughly 168-183 MiB; path strings,
    child-vector spare capacity, and transient directory listings are additional, so this is a fail-safe ceiling,
    not a promised memory budget.
11. Request validation rejects a proposed root destination that is lexically inside that source. This prevents
    recursive copy and permanently failing same-tree move requests before scanning or filesystem mutation. The
    comparison is component-aware and follows the platform spelling policy; aliases through directory links remain
    outside this lexical rule.

### Accepted corner cases

The file-operation engine does not lock filesystem namespaces against concurrent changes by other processes. In
this section, "data race" means that external filesystem activity races the operation; it does not mean an
unsynchronized C++ memory access.

Source manifests and replacement decisions are path-bound rather than filesystem-identity-bound. The following
time-of-check-to-time-of-use sequences are accepted limitations:

1. After a source entry is scanned, another process may move it away and place a different same-kind entry at the
   same path. A later source-side action can then open, rename, modify, or remove the replacement. This includes
   permanent deletion, per-child rename during a merged move, directory-timestamp handling, and removal of an owned
   source after a copy-based move has published its destination.
2. After the user authorizes replacement of an existing destination entry, another process may replace that entry
   at the same path before publication. The authorization applies to the path, so publication may replace the new
   occupant without another prompt. A staged copy makes this interval potentially as long as the transfer itself.
3. During manifest construction, a descendant may disappear between its parent's enumeration and its own
   inspection or directory listing. A resulting `NotFound` omits that descendant from the complete manifest; it
   does not make the operation fail merely because the race occurred later for a directory than for a file. A
   selected root disappearing retains its existing operation-level outcome.
4. On Windows, an authorized replacement that initially fails because its regular-file destination is read-only
   clears `FILE_ATTRIBUTE_READONLY` and immediately retries the native replacement. The destination can be
   substituted between the attribute inspection, attribute change, and replacement; those steps remain path-bound
   and may therefore change or replace the new occupant. If the retry fails, restoring the original attribute mask
   is path-bound in the same way. A restoration failure is reported, but the race itself is deliberate accepted
   behavior, not a defect. Moving the destination aside is intentionally avoided: it would add an absent-destination
   window, crash/rollback states, and an old-file cleanup that still requires clearing the read-only attribute.

Rechecking kind or size immediately before mutation would detect some substitutions but would neither identify an
entry nor close the final check-to-act window. Size checks would also reject legitimate in-place modification of
the original file while missing a same-sized replacement. Do not add such heuristics as if they were an identity
guarantee. A future hardening pass would need stable identities carried through the manifest and replacement
authorization plus handle-bound or conditional native mutation where the platform supports it.

`RequireAbsent` is strictly non-clobbering when the platform and filesystem provide a native exclusive-rename
primitive. If that primitive reports itself unsupported, the mutator deliberately degrades to a fresh no-follow
destination check immediately followed by plain rename. This preserves otherwise workable operations on such
filesystems and on best-effort FreeBSD, but accepts a narrow TOCTOU window in which an entry created after the check
can be replaced. Failing every fresh publication on those filesystems is not an acceptable compatibility tradeoff.
Hard-link-plus-unlink emulation is intentionally rejected: it is type- and filesystem-dependent, does not cover
directories, and introduces additional partial/crash states for disproportionate complexity.

Staged publication does not synchronize the destination directory after renaming the staging entry. File Commander
therefore relies on the filesystem and kernel's normal namespace persistence: a successful publication, and in
particular a copy-based move whose source is subsequently removed, is not a transaction guaranteed to survive sudden
power loss. A directory synchronization barrier per published file would inhibit normal write batching, add latency
and storage traffic, and still would not provide a simple equivalent guarantee on every supported platform. The
existing pre-publication data flush is a narrower conservative policy used only when publication or cleanup destroys
the other copy; it prevents File Commander from deliberately proceeding while the new contents remain buffered, but
does not imply a broader crash-durability contract and may be reevaluated independently.

Staged copy maps each source chunk and passes that mapping directly as the input buffer to the native destination
write; File Commander never dereferences the mapping itself. Supported kernels are expected to turn a fault while
copying that user buffer into a failed or partial write, which the operation already handles. Installing a process-wide
`SIGBUS` handler would not itself provide recovery: safe continuation would still require per-thread jump state around
each write, correct handling of unrelated signals, and a separate Windows mechanism. The residual possibility that a
POSIX implementation signals instead of reporting the buffer fault is accepted. If this is reproduced on a supported
platform, replace mapping with a reusable `pread()` buffer rather than adding signal recovery.

Summary counters intentionally do not partition every node in a scanned manifest. When delete or owned move retains
an ancestor directory because a descendant was skipped or otherwise remains at the source, that ancestor advances
item progress so a non-cancelled traversal that resolves every manifest node reaches its exact total, but it enters
no summary bucket. It was
neither completed nor independently skipped or failed; counting it as skipped would make one user decision multiply
with directory depth. `CompletionStatus::Completed` means the executor finished while honoring the user's decisions,
not that every initially requested filesystem effect occurred. The originating `skippedItems` or other outcome counter
describes the omission. Cancellation still leaves unvisited nodes unresolved and need not reach the progress total.
Permanent delete consequently has no terminal `Failed` route: each removal problem resolves as Retry, Skip, or
Cancel; Skip contributes to `skippedItems`, while Cancel stops the operation.

Inline rename (`inlinerename.{h,cpp}`) is a separate synchronous command with its own replacement matrix, reusing
the name, inspection, identity, and native rename primitives. Test-only deterministic errors/barriers are isolated
in `operationtesthooks.{h,cpp}` and compile out without `FILE_OPERATIONS_TEST_HOOKS`.

## Traversal and watchers

`scanDirectory()` in `directoryscanner.{h,cpp}` is shared by flattened panel display, search, and folder comparison;
file operations and statistics use their own traversals. It can follow directory links, reports whether an item was
reached through one, and breaks cycles using `resolvedObjectId()` rather than path strings. That distinction matters
on Windows, where canonical path text does not reliably resolve junction targets.

Each panel owns a platform watcher selected in `cpanel.h`:

- Windows uses `CFileSystemWatcherWindows`, backed by native change notification handles.
- Other platforms use `CFileSystemWatcherTimerBased`, which compares periodic snapshots. A path generation rejects
  obsolete scans. `CPanel` calls `captureBaselineState()` immediately before its own enumeration so a change made
  after navigation cannot disappear into a later lazy baseline.

Flattened recursive display disarms the single-directory watcher. Inactive tabs also disarm it.

## Other core areas

| Area | Source and boundary |
|------|---------------------|
| Volumes | `diskenumerator/`; periodic enumeration with UI-queued observer delivery. Initial enumeration is synchronous because restored panel paths depend on it. |
| Search | `filesearchengine/`; one interruptible outer thread, with a bounded worker pool only for content searches. The listener must outlive the worker; use `waitForSearchToFinish()` before teardown. |
| Folder/file comparison | `filecomparator/`; synchronous byte/tree primitives plus `CFileComparator` as the single-file threaded wrapper. Pairing policy is explicit in `PairingMode`. |
| Shell integration | `shell/`; platform clipboard, context menus, launching, and supported native deletion backends. |
| Favorites | `favoritelocationslist/`; nested locations persisted under its supplied settings key. |
| Icons | `iconprovider/`; extension guessing is the fast path because it avoids disk access. |
| Statistics/helpers | `filesystemhelpers/`, `filesystemhelperfunctions.{h,cpp}`; recursive statistics, path conversion, and resolved native identity. |

See [qt-ui.md](qt-ui.md) for consumers, [plugins.md](plugins.md) for the plugin surface, and
[persistence.md](persistence.md) for settings ownership.
