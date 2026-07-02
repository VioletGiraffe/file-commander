# Per-panel tabs (cross-cutting feature)

Total Commander-style tabs: each side has >=1 tab, each tab is an independent folder view. Feature is
complete and shipped. The design's central trick keeps ~all pre-tabs call sites unchanged.

## The linchpin

`CController::panel(Panel p)` returns the side's **active tab's** `CPanel`. A tab *is* a `CPanel`. Code that
predates tabs (navigation, the file-list models, selection, file ops) keeps calling `panel(side)` and
transparently operates on whatever tab is active. Only code that manages tabs themselves is tab-aware.

## Identity: tabs are addressed by ID, not position

- Core assigns each tab a stable `qulonglong` id (`CController::_nextTabId`, starts at 1; 0 = "no tab").
  `CPanel::id()` exposes it. The id never changes for a tab's lifetime and survives reorder/resize of the
  underlying vector.
- The UI's `QTabBar` stores the core id as each tab's `tabData` (`CPanelWidget::tabIdAt(pos)`), so a UI
  position is converted to a core id whenever core is called. This is what makes drag-reorder and
  close-arbitrary-tab safe.

## Core side (`CController`)

- `std::array<TabList,2> _panels`, `TabList { vector<unique_ptr<CPanel>> tabs; size_t activeTab }`.
- API (all id-based): `addTab(p, path, activate=true) -> id`, `closeTab(p, id)` (refuses to remove the last
  tab), `setActiveTab(p, id)`, `moveTabPosition(p, id, newPos)`, `tabCount`, `activeTabId`,
  `tabIds` (display order), `tabPath(p,id)`, `tabName(p,id)`.
- Helpers: `createTab` (make CPanel, wire listeners, append), `attachListenersToTab` (re-attach the side's
  recorded contents/cursor listeners + the plugin engine to a new tab), `switchActiveTab`
  (deactivate old / activate new — callers skip it when already active), `tabIndexById`.
- **Activation cost:** `switchActiveTab` calls `CPanel::setActive(false)` on the outgoing tab (which
  **releases its filesystem watch handle**) and `setActive(true)` on the incoming (re-arms the watch + a
  refresh-on-activate). So inactive tabs are cheap — they don't hold watch handles or get change events.
- The shared `_panelWorkerPool` is injected into every tab's `CPanel` and is **declared before `_panels`**
  so it outlives them. Each tab tags its pool tasks with `_taskTag` and retires them in its dtor.

## UI side (`CPanelWidget`)

- `std::vector<PanelTab> _tabs` index-aligned with the `QTabBar` and (by id mapping) with core's tab list.
  `PanelTab = { CFileListModel*, CFileListSortFilterProxyModel*, QItemSelectionModel*, QByteArray headerState }`.
- **One shared `CFileListView`.** `activateTab(index)` points the view at that tab's triplet and
  saves/restores `headerState` (per-tab column widths/order/visibility; sort-indicator bits ignored because
  the proxy owns the sort). `_model`/`_sortModel`/`_selectionModel` always alias the active triplet so the
  rest of the widget is tab-agnostic.
- `populateTriplet(tab)` wires a fresh model/proxy/selection trio.
- User ops: `createNewTab` (current folder, switch to it), `closeCurrentTab`/`closeTabById`/`closeAllOtherTabs`,
  `duplicateTab`, `switchToNext/PreviousTab`, `switchToTabByPosition` (Ctrl+1..9),
  `openCurrentItemInNewTab` (Ctrl+Up) + middle-click folder (`onItemMiddleClicked`) both via
  `tryOpenItemInNewTab` -> `openPathInNewTab(path, activate)` (activate=false = background tab).
- Tab-bar plumbing: `onTabBarCurrentChanged`, `onTabBarCloseRequested`, `onTabBarTabMoved` (mirrors a drag
  into both `_tabs` and core via `moveTabPosition`), `showContextMenuForTab`, `updateTabBarVisibility` (bar
  hidden while a single tab), `updateTabText`/`updateTabBarVisibility`.
- `CMainWindow` owns a programmatic **"Tabs" menu** (New Ctrl+T / Close Ctrl+W / Next Ctrl+Tab /
  Prev Ctrl+Shift+Tab) routing to the active panel widget.

## Model validity invariant

Both `CFileListModel` and `CFileListSortFilterProxyModel` read item data through `_controller.panel(side)`
= the **active** tab. A per-tab model is therefore only correct while its tab is active. The architecture
guarantees only the active triplet is ever attached to the view, so only it is ever queried — but this is an
invariant to respect when touching tab-switch code (see [oddities.md](oddities.md)).

## Persistence

Tabs are saved/restored by `CController` (not `CPanel`). Per side: the list of tab paths
(`KEY_*PANEL_TABS`), the active index (`KEY_*PANEL_ACTIVE_TAB`), per-tab cursor item hashes
(`KEY_*PANEL_TAB_CURSORS`), and — for back-compat — the active tab's path mirrored to the legacy
`KEY_*PANEL_PATH`. `restorePanelState` migrates old single-path installs into a single tab. Only the active
tab's back/forward history is persisted (`KEY_HISTORY_*`). See [persistence.md](persistence.md).

## Deferred / nice-to-have backlog

Per project memory `plan-ui-tabs`: a short list of deferred niceties remains (e.g. session/named tabs,
locked tabs). Re-check that note and `git log` before assuming status.
