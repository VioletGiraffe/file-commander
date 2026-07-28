# Qt GUI app (`qt-app/`)

`qt-app.pro` is the authoritative source list for the `FileCommander` Qt Widgets executable. Designer forms live
beside their `.h/.cpp` owners. The GUI reaches filesystem and panel state through `CController`.

## Source map

| Area | Start here |
|------|------------|
| Startup and application-wide settings | `src/main.cpp` |
| Commands, focus, shortcuts, and top-level ownership | `src/cmainwindow.{h,cpp,ui}` |
| One visual side and its tabs | `src/panel/cpanelwidget.{h,cpp,ui}` |
| File-list view/model/proxy/delegate | `src/panel/filelistwidget/` |
| Quick view | `src/panel/cpaneldisplaycontroller.{h,cpp}` |
| Copy/move/delete launch and UI | `src/progressdialogs/` |
| Search UI | `src/filessearchdialog/` |
| Statistics and folder comparison | `src/tools/` |
| Settings pages | `src/settings/` |
| Favorites editor | `src/favoritelocationseditor/` |

`main.cpp` applies settings that must precede widget creation, initializes platform services, then owns the main
event loop. `--test-launch` schedules an automatic quit for the CI smoke test.

## Main window

`CMainWindow` owns `CController`, the two `CPanelWidget`s, and two `CPanelDisplayController`s. Its “current” and
“other” panel pointers follow focus rather than fixed left/right position. It is the command-routing hub: actions
gather UI state, call a core or launch boundary, and own the resulting dialogs. Read its header for the current
command set rather than maintaining another list here.

The UI timer enters `CController::uiThreadTimerTick()` to drain cross-thread work. Concurrent file-operation dialogs
register with the main window only for background placement/lifetime tracking; each dialog owns its job. Plugin
`MenuTree`s are materialized into the Commands menu here.

## Panel widget and tabs

Each side has one `CPanelWidget` and one shared `CFileListView`. Every tab owns a `PanelTab` containing its own
`CFileListModel`, sort/filter proxy, selection model, and saved header state. `_model`, `_sortModel`, and
`_selectionModel` are aliases to the active triplet.

The QTabBar position, the `_tabs` vector position, and the active core tab ordering are kept aligned, but calls into
the controller use the stable tab ID stored in `QTabBar::tabData`. Closing or reordering multiple tabs must resolve
positions from IDs immediately before acting; positions are not durable identities.

Only the active triplet may be attached to the view. Both the model and proxy obtain item data through
`CController::panel(side)`, which means a background tab's model would read the active tab's panel if queried. Tab
switching must set the incoming model, selection model, sort indicator, and header state as one operation. The exact
Qt ordering workaround is documented in `CPanelWidget::activateTab()`.

Panel notifications arrive from every tab on the side. `displaysTab()` filters by both side and stable tab ID before
the widget updates the active model or cursor. Invalidation clears display only; committed contents repopulate it.
See [tabs.md](tabs.md) and [threading.md](threading.md).

`CPanelWidget::savePanelState()`/`restorePanelState()` concern column header state, not core tab/path persistence.

## File-list MVC

- `CFileListView` implements orthodox selection and keyboard/mouse behavior. Enter/double-click uses an observer
  chain rather than a Qt signal because the first consumer may stop propagation.
- `CFileListModel` stores only item hashes. Cell data is resolved live from the active controller panel; inline edit
  emits the hash and proposed name.
- `CFileListSortFilterProxyModel` owns sort/filter state and natural ordering. Directories and the synthetic parent
  entry receive special ordering.
- The delegate and focus style own painting only. Drag/drop request construction routes through the same operation
  launch boundary as commands.

Read the corresponding headers and implementations for roles, signals, and interaction details.

## File-operation UI

`progressdialogs/fileoperationlaunch.{h,cpp}` is the boundary from raw UI selection/destination text to a typed core
request. It chooses `DestinationIntent` exactly once, before parsing removes a trailing separator: `/` on every
platform and `\\` on Windows explicitly mean "into this directory," even when the directory does not exist yet.
It also selects the platform deletion backend; only `DeletionBackend::InternalJob` uses the custom permanent-delete
engine.

`CFileOperationDialog` owns one `CFileOperationJob`, drains typed events on a timer, and formats rather than invents
policy. `CFileOperationPrompt` renders only the actions delivered in a `DecisionRequest`. Keep decision capability
in the core policy table, not in parallel UI logic.

The dialog controls its own visibility and, by default, lifetime:

1. Callers register it, call `start()`, and do not call `show()`; quick successful operations never appear.
2. A decision request may force it visible. Background mode uses an injected placement provider.
3. Completion remains visible only when the summary needs attention; other outcomes dispose silently.
4. Closing during execution suppresses later reappearance but does not make the dialog's job/listener lifetime
   optional. Tests or embedded owners call `disableSelfDisposal()`.

Cancellation confirmation pauses the job while the modal question is open. Event draining has a reentrancy guard
because decision presentation runs a nested Qt event loop.

## Quick view and plugins

`CPanelDisplayController` switches a `QStackedWidget` between the normal panel and a plugin window. It owns the
quick-view window through the plugin-defined custom-deleter pointer, ensuring destruction happens in the dynamic
library that allocated it. See [plugins.md](plugins.md).

## Tests

Automated file-operation UI tests live in `qt-app/gui-tests/fileoperations/` and are included by the core test
aggregate. `qt-app/gui-tests/combobox/` is a manual history-combobox harness. See [build-ci-deps.md](build-ci-deps.md)
for discovery and runtime knobs.
