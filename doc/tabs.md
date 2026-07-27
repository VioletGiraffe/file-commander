# Per-panel tabs

Tabs cross core ownership, UI model/view state, notifications, worker lifetime, and persistence. Start with
`CController` and `CPanelWidget`; their headers contain the current tab operations.

## Core model

A tab is an independent `CPanel`. Each side's `CController::TabList` owns one or more panels and identifies the
active position. `CController::panel(side)` returns the active tab, which keeps non-tab-specific navigation,
selection, and operation code unchanged.

Tabs have stable, nonzero `qulonglong` IDs. IDs survive vector growth and reordering; positions do not. Core APIs
that identify a tab use its ID. The QTabBar stores that ID as tab data, and the UI resolves it immediately before
close/reorder operations rather than retaining a position that another operation may shift.

The controller records side-wide panel/content listeners and attaches them, plus the plugin engine, to every new
tab. Notifications therefore identify the source tab. A consumer representing the visible tab must compare the ID
with `activeTabId(side)`; folder path cannot substitute because duplicate tabs may show the same path.

## UI model invariant

`CPanelWidget` owns one shared `CFileListView` and a position-aligned `std::vector<PanelTab>`. Each `PanelTab` owns a
model, sort/filter proxy, selection model, and header-state snapshot. The widget's `_model`, `_sortModel`, and
`_selectionModel` alias the active triplet.

Both models resolve data through `CController::panel(side)`, not through a stored non-active `CPanel`. Consequently:

1. Only the active triplet may be queried or attached to the shared view.
2. Switching tabs must swap model and selection ownership together and restore that tab's sort/header state.
3. Background-tab notifications must be filtered before touching the active triplet.

The Qt-specific sort-indicator ordering needed during `setModel()` is documented in
`CPanelWidget::activateTab()` and should remain there.

## Lifetime and resources

All panels share `CController::_panelWorkerPool`. Each panel tags its tasks and retires its tag on destruction, so
closing one tab cannot leave a task touching freed panel state and does not wait for unrelated tabs. The pool is
declared before the tab lists so it outlives them.

Only active tabs hold a filesystem watch. Deactivation releases it; activation re-arms it and refreshes because
changes while inactive were intentionally not observed. A newly opened background tab is deactivated after its
initial path is set for the same reason.

## Persistence

`CController`, not `CPanel`, persists tab paths, active position, and per-tab cursor hashes and migrates the legacy
single-path settings. Back/forward history and the side-wide visited-locations log have different lifetimes; see
[persistence.md](persistence.md) rather than duplicating their key/state machine here.

UI-only state is separate: `CPanelWidget` saves column header state, each live tab retains its own sort/header
snapshot, and the recently-closed-tab stack retains paths only for the current process.

## Change checklist

When changing tab creation, activation, close, or reorder, verify stable ID mapping, core/UI ordering, active-triplet
attachment, notification filtering, watcher activation, pool task retirement, and persistence independently. See
[threading.md](threading.md) for the panel publication contract.
