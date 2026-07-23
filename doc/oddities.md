# Oddities, smells & open questions

Observations noted while documenting — **not** a code review. Confidence labeled; verify before acting.
Nothing here is known to break the app at runtime except where stated.

## Confirmed defects

- **CI release body has a stray brace.** `.github/workflows/CI.yml` last line:
  `body: ${{ steps.changelog.outputs.body }}}` — three closing braces; the GitHub expression closes with
  `}}`. Effect: every generated GitHub Release body gets a literal trailing `}` appended. Cosmetic, but
  real. **Fixed 2026-07-01** — removed the stray brace. (High confidence — read directly.)

## Cosmetic / harmless-but-note

- **Dead settings key:** `KEY_PROMPT_DIALOG_GEOMETRY` (note the "Propmpt" misspelling) belonged to the
  removed `CPromptDialog`; its replacement prompt does not persist geometry, so the key is now unused. Left
  in `settings.h` (harmless); a future cleanup could drop it.
- **Doc vs build C++ standard:** README says "C++20 minimum", but `global.pri` forces `c++2b` (C++23) and
  MSVC `/std:c++latest`. The real floor is higher than documented. Not a bug; update the README if it
  matters.
- **Listener naming drift in `CPanel`:** method `addCurrentItemChangeListener` takes a
  `CursorPositionListener`, and the member is `_currentItemChangeListener` (singular) while the sibling
  lists are plural (`_panelContentsChangedListeners`, `_currentPathChangedListeners`). Two names for one
  concept (current item == cursor position). Cosmetic.

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
- **Inactive tabs drop their watch handle.** `CPanel::setActive(false)` releases the filesystem watch;
  `setActive(true)` re-arms + refreshes. Correct, but it means changes made while a tab was inactive are
  only picked up on the refresh-on-activate — a deliberate trade-off worth remembering when debugging
  "stale list" reports.

## Looks alarming, is actually fine (so future-me doesn't re-investigate)

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
- `CWcxPluginHost` needs `setWcxSearchPath` before it loads anything.

## Open questions (need code reading to answer)

- **WCX archive integration depth.** `CWcxPluginHost` loads `.wcx` libraries, but how far archive contents
  are browsable as a panel folder is unclear from the headers. Trace `cwcxpluginhost.cpp` before assuming
  full archive-as-folder support.
- **Bundle type.** `FileSystemObjectType::Bundle` exists (macOS app bundles?). Confirm where it's produced
  and how panels treat it cross-platform.
- **`_workerThreadPool` vs `_panelWorkerPool`.** The controller has both a general pool and the shared panel
  pool. Confirm nothing posts panel-lifetime-coupled work to the general pool (which has no retire-by-tag
  discipline tied to panels).
