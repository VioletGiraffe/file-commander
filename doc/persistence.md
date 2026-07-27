# Persistence & settings

Backed by `CSettings` (qtutils wrapper over `QSettings`). App/org names ("File Commander"/"GitHubSoft", set
in `main.cpp`) determine the store location (Windows registry / ini per platform). All keys are `#define`d
in `file-commander-core/include/settings.h` (the only public core header). `QSL(...)` is a static `QString`
literal helper.

## Save/restore flow (centralized in `CController`)

Persistence is managed by `CController`.

- **Restore (`restorePanelState(p)`, at startup):** read `KEY_*PANEL_TABS`; for each path create a tab; set
  active tab from `KEY_*PANEL_ACTIVE_TAB`; seed per-tab cursor from `KEY_*PANEL_TAB_CURSORS`; seed the active
  tab's history from `KEY_HISTORY_*`. **Migration:** an old install with only `KEY_*PANEL_PATH` becomes a
  single tab.
- **Save (`savePanelState(p)`):** write all tab paths + active index + per-tab cursors + mirror active path
  to `KEY_*PANEL_PATH`. **Deduped** against `_lastSavedTabSignature[side]` so the frequent watcher-driven
  refreshes don't rewrite settings every tick. Driven by `onPanelContentsChanged` (controller listens to its
  own tabs) and on shutdown (dtor).
- **History/visited (`saveHistoryList(p)`):** writes the active tab's back/forward history + the side's
  visited-locations log. `CMainWindow` saves it every five minutes, and `CController` saves it again on
  shutdown; it is deliberately not written on every navigation.
- **Visited-locations** are appended live in `onCurrentPathChanged` (fires only on an actual directory
  change, not every refresh).

## Other persisted bits (outside `settings.h`)

- **View header state** (column widths/order/visibility) — per tab, held in `PanelTab::headerState` in the
  UI; `CPanelWidget::savePanelState`/`restorePanelState` handle the `QByteArray`. Distinct from the
  controller's path/tab persistence above.
- **Window geometry / splitter** — standard `QMainWindow`/widget state save.

See [tabs.md](tabs.md) for how tab identity maps onto these keys.
