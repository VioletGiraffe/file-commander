# Code-review plan (segmentation)

How to segment a review/audit of this codebase so it's tractable and front-loads the highest-value findings.
Grounded in the architecture (see [README.md](README.md)) and the known sharp edges ([oddities.md](oddities.md)).

## How to use this

- **Full audit:** work the segments in tier order (1 -> 4). Tier 1 first means a core-level finding is known
  before reviewing the UI that depends on it.
- **Diff-driven review** (the usual case, e.g. `/code-review`): map each changed file to its segment below,
  review that segment's *focus list*, **plus** always run the Cross-cutting checklist against the diff.
- One segment ~= one focused sitting or one subagent. File sets are bounded so a segment fits in context.
- Suggested effort maps to `/code-review` levels (low/medium/high/max). Data-integrity and concurrency get
  `high`+; self-contained low-risk gets `medium`.

## Coverage

Segments A-J partition **all 63 product `.cpp`/`.mm` files** (core + qt-app + shipped plugins) — one home
segment each; B is a concurrency overlay over A/C/G/I, not a separate bucket. Every header folds into the
segment of its matching translation unit; pure-interface/enum/data-structure headers
(`fileoperationresultcode.h`, `operationcodes.h`, `detail/*.h`, `diskenumerator/*.hpp`, `columns.h`,
`version.h`, `cfileoperationdialogbase.h`) go with their owning segment. In-scope but not named individually
above: plugin-interface bases `cfilecommander{plugin,toolplugin,viewerplugin}.cpp` + the viewer
window/widget sources -> H; `iconprovider/ciconprovider.cpp` (facade over the impl) -> I.

**Deliberately outside A-J — review as separate buckets, like the submodules:**
- Submodules: `qtutils cpputils cpp-template-utils thin_io text-encoding-detector image-processing github-releases-autoupdater`.
- Vendored 3rdparty: `3rdparty/ankerl/unordered_dense`, `3rdparty/magic_enum`, textviewer `qutepart-cpp`.
- Test suites: `file-commander-core/core-tests/**`, `qt-app/gui-tests/**` (own pass; cross-check against the code they exercise).
- Build/infra: `*.pro`/`*.pri`, `.github/workflows/CI.yml`, `installer/**`.
- `.ui` forms are reviewed with their widget (D/E/J), not as standalone code.

## Cross-cutting checklist (apply to every segment)

1. **Lifetime / ownership** — raw pointers across boundaries, who deletes what, dangling on tab/dialog close.
   Hot spot: anything posting to `_panelWorkerPool` must be `_taskTag`-tagged and retired in the dtor.
2. **Concurrency** — every field touched off the UI thread must be guarded or atomic; check queue-drain
   ordering and re-entrancy on the recursive mutexes. (Project rule: never skip a dedicated threading pass —
   see [threading.md](threading.md).)
3. **Error handling** — `FileOperationResultCode` / `lastErrorMessage` propagated, not swallowed; native
   API failures checked; partial-state cleanup on failure/cancel.
4. **Hash identity** — code keying on `qulonglong` item hash must tolerate collisions/stale hashes (fall back
   to first item, never crash/misfile).
5. **Cross-platform** — `#ifdef _WIN32`/`win*{}` branches have parity; a change to one OS path has the
   matching change (or a deliberate, noted gap) on the others.
6. **Project coding style** — match local patterns; `emplace_back`-and-edit; std int types over Qt typedefs
   in new code; comment only the surprising; 140-col lines; ASCII in comments. (See global guidance + memory.)
7. **The sharp edges** from [oddities.md](oddities.md) — items 5-7 especially (active-tab model reads,
   shared-pool tab lifetime, inactive-tab watch handle).

## Tier 1 — highest risk, behavior-heavy, data-integrity (review first)

### A. File-operation engine
- **Files:** `fileoperations/coperationperformer.{h,cpp}`, `cfilemanipulator.{h,cpp}`, `operationcodes.h`.
- **Why first:** data-loss potential; the most consequential code in the repo; never read at `.cpp` level yet.
- **Focus:** the worker<->UI condition-variable handshake (deadlock/lost-wakeup/spurious-wakeup); cancel/pause
  at every safe point; partial-copy cleanup on error/cancel; `_globalResponses` "...All" memory correctness;
  `moveWithinSameDrive` rename fast-path vs copy+delete fallback; overwrite semantics + permission/timestamp
  transfer; chunked copy (`copyChunk`/`bytesCopied`/`cancelCopy`) and `thin_io::file` use; the `NextAction`
  retry loop (infinite-retry / progress-not-advancing hazards); free space check (`hrNotEnoughSpace`).
- **Effort:** max. **Delegation:** keep in main thread or a single dedicated subagent — findings interrelate.

### B. Concurrency & lifetime (cross-cutting sweep)
- **Files:** the threading touchpoints — `ccontroller.cpp` (queues, `execOn*Thread`, `uiThreadTimerTick`),
  `cpanel.cpp` (`_taskTag`/retire, `_fileListAndCurrentDirMutex`, watcher poll), `cvolumeenumerator.cpp`
  (recursive mutex), both `cfilesystemwatcher*.cpp`, `cfilesearchengine.cpp`. Plus A's threading.
- **Why:** mandated dedicated pass; tab close + shared pool is the prime use-after-free risk.
- **Focus:** tag-and-retire completeness; UAF on tab/dialog teardown; data races on the file list; queue
  draining vs widget teardown; recursive-mutex re-entry assumptions; watcher arm/disarm on activate/deactivate.
- **Effort:** max. **Delegation:** main thread (must correlate across files).

### C. Controller + tab/persistence logic
- **Files:** `ccontroller.cpp`, `cpanel.cpp`.
- **Focus:** tab id lifecycle (`_nextTabId`, no reuse-after-free); last-tab invariant in `closeTab`;
  `activeTab` index bounds after close/reorder; `switchActiveTab`/`tabIndexById`; `setActive` watch
  arm/disarm + refresh-on-activate; persistence — `restorePanelState` legacy migration, `savePanelState`
  dedup signature, `saveHistoryList` shutdown-only, parallel arrays (tabs/cursors) staying aligned;
  listener re-attach to new tabs. (See [tabs.md](tabs.md), [persistence.md](persistence.md).)
- **Effort:** high. **Delegation:** subagent OK (self-contained once A/B's concurrency model is known).

## Tier 2 — UI correctness

### D. Panel widget & tab/view synchronization
- **Files:** `panel/cpanelwidget.{cpp,ui}`, `cpaneldisplaycontroller.cpp`.
- **Focus:** the three-way alignment `_tabs[] <-> QTabBar position <-> controller tab id` under
  close/reorder/duplicate/close-others; `activateTab` triplet swap + per-tab `headerState` save/restore
  (no bleed between tabs); `tabIdAt` mapping; model-triplet construction/teardown (sharp edge #5: only the
  active triplet may be queried); quick-view stacked-widget swap lifetime.
- **Effort:** high. **Delegation:** subagent OK.

### E. File-list MVC
- **Files:** `panel/filelistwidget/model/cfilelistmodel.cpp`, `cfilelistsortfilterproxymodel.cpp`,
  `cfilelistview.cpp`, `delegate/cfilelistitemdelegate.cpp`, `cfilelistfilterdialog.cpp`, `cfocusframestyle.cpp`.
- **Focus:** `data()` reading the active tab; **drag & drop** (`mimeData`/`dropMimeData`/`canDropMimeData` —
  this triggers real file ops, check source/dest correctness + self-drop + modifier semantics); natural-sort
  edge cases ("..", dirs-first, locale); the custom selection/keyboard handling (TC semantics, region select,
  PgUp/PgDn); inline rename (`itemEdited` -> rename path); index<->hash mapping helpers.
- **Effort:** high (drag-drop) / medium (rest). **Delegation:** subagent OK.

## Tier 3 — filesystem core, platform integration, extensibility

### F. Filesystem object & helpers
- **Files:** `cfilesystemobject.cpp`, `filesystemhelperfunctions.cpp`, `filesystemhelpers/*.cpp`,
  `filestatistics.cpp`, `directoryscanner.cpp` (already read).
- **Focus:** **hash computation** (determinism across runs, collision behavior — everything keys on it);
  `refreshInfo`/lazy time caching; type detection incl. `Bundle` (open question #13); `isMovableTo`,
  symlink handling; the `CFILESYSTEMOBJECT_TEST` mock seam (parity between mock and real `QFileInfo`/`QDir`).
- **Effort:** high (hashing) / medium (rest). **Delegation:** subagent OK.

### G. Platform integration (per-OS, test-on-each)
- **Files:** `shell/cshell.cpp` + `cshell_mac.mm`, `diskenumerator/cvolumeenumerator_impl_{win,mac,linux,freebsd}.cpp`,
  `cfilesystemwatcherwindows.cpp`, `cfilesystemwatchertimerbased.cpp`.
- **Focus:** Win32/COM correctness (init/teardown balance, handle/`HGLOBAL` leaks, error checks on shell
  calls), recycle-bin + clipboard + run-as-admin paths; the native event-filter watcher (message handling,
  thread-safety); timer-based watcher diff (`std::set<FileSystemInfoWrapper>` — missed/duplicate change
  detection); volume enumeration per OS. Flag what can only be validated by running on each OS.
- **Effort:** high. **Delegation:** subagent per OS-file is fine; reviewer likely can't run all 4 platforms.

### H. Plugin system
- **Files:** `pluginengine/cpluginengine.cpp`, `plugininterface/cpluginproxy.cpp`, `cpluginwindow.cpp`,
  `wcx/cwcxpluginhost.cpp`, the 3 plugins under `plugins/`.
- **Focus:** plugin/`QLibrary` lifetime (unload ordering vs live windows); the cross-DLL `WindowPtr` deleter
  discipline (allocate/free in the same module); proxy state-snapshot thread-safety; `loadPlugins` glob +
  symlink skip; **WCX integration depth** (open question #12 — how far archive-as-folder actually works).
- **Effort:** medium (high for WCX). **Delegation:** subagent OK (self-contained).

## Tier 4 — self-contained / lower risk

### I. Standalone subsystems
- **Files:** `filecomparator/cfilecomparator.cpp`, `filesearchengine/cfilesearchengine.cpp` (thread covered in B),
  `iconprovider/ciconproviderimpl.cpp`, `favoritelocationslist/cfavoritelocations.cpp`.
- **Focus:** comparator correctness (covered by `filecomparator_test` — cross-check); favorites
  load/save + nested tree; icon caching + settings reaction.
- **Effort:** medium. **Delegation:** subagent OK.

### J. App shell & dialogs
- **Files:** `main.cpp`, `cmainwindow.{cpp,ui}`, `progressdialogs/*`, `settings/*` (4 pages),
  `favoritelocationseditor/*`, `filessearchdialog/*`, `aboutdialog/*`, `tools/CFileStatsWindow.*`.
- **Focus:** file-op dialog orchestration (`_activeFileOperationDialogs` lifetime, observer wiring to
  `COperationPerformer`, background cascade); command-line routing + history; focus management; settings
  apply/round-trip; menu/action wiring incl. the programmatic Tabs & Tools menus.
- **Effort:** medium. **Delegation:** subagent OK.

## Execution order & dependencies

```
A (file ops) ─┐
B (concurrency)├─ Tier 1: do together / first; B's model informs everything
C (controller)┘
        │ core behavior settled
        v
D (panel widget) ── E (MVC)            Tier 2: UI, depends on C
        │
        v
F (fso) ── G (platform) ── H (plugins) Tier 3: can parallelize across subagents
        │
        v
I (subsystems) ── J (app shell)        Tier 4: last, lowest risk
```

- A + B + C share the concurrency/lifetime model — review as a cluster, B's findings feed A and C.
- Tiers 3 and 4 are mutually independent -> good parallel-subagent candidates; Tier 1 stays in the main
  thread for finding correlation.
- For a **diff** that spans tabs, expect to touch C + D together (tabs are split core/UI by design).

## Tooling

- `/code-review <level>` reviews the **current diff** — use per-segment by scoping the change, or `--fix`
  to apply, `--comment` to post inline. `/code-review ultra` is the deep multi-agent cloud pass (user-triggered).
- For auditing **existing** (un-diffed) code, drive a segment by handing a subagent its file set + the
  segment's focus list + the Cross-cutting checklist; ask for factual findings with file:line citations.
- Keep `coperationperformer.cpp` and the threading sweep (A/B) in the main thread — their findings are too
  interdependent to summarize losslessly through a subagent.
