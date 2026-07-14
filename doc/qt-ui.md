# Qt GUI app (`qt-app/`)

The `FileCommander` executable. Qt Widgets. Depends on `core` + all submodules + the three plugins (so they
build first). Sources in `qt-app/src/`. `.ui` files are Qt Designer forms; widget classes pair `.h/.cpp/.ui`.

## Entry point (`src/main.cpp`)

1. `AdvancedAssert::setLoggingFunc` -> `qInfo`. App/org name = "File Commander" / "GitHubSoft" (drives
   `QSettings` location). `CO_INIT_HELPER(COINIT_APARTMENTTHREADED)` (COM for shell integration).
2. Asserts build-time and runtime Qt versions match. High-DPI rounding = `PassThrough`.
3. Applies saved **color scheme** (Qt 6.8+ `QStyleHints::setColorScheme`), **style**, and **stylesheet**
   from settings before building the window. On Windows dark mode, lightens `AlternateBase` (fixes Qt's
   poor default alternate-row color).
4. Loads bundled **Roboto Mono** font (`:/fonts/Roboto Mono.ttf`); bumps default app font +1 pt.
5. Creates `CMainWindow`, `updateInterface()`, `app.exec()`.
6. `--test-launch` arg: `QTimer::singleShot(5000, quit)` — the CI smoke test.

## CMainWindow (`src/cmainwindow.{h,cpp,ui}`)

`final : QMainWindow`, singleton via `get()`. Implements `FileListReturnPressedObserver` and
`PanelContentsChangedListener`. **Owns the `CController`** (`std::unique_ptr`). This is the command hub —
nearly every menu/toolbar/shortcut action is a private method here.

- Owns two `CPanelWidget*` (`_currentFileList`, `_otherFileList`) and two `CPanelDisplayController`
  (`_leftPanelDisplayController`, `_rightPanelDisplayController`). "current" vs "other" follow focus, not side.
- `initCore` wires the controller to the widgets; `initButtons`/`initActions` build the command set;
  `updateInterface` applies settings.
- **File-op slots:** `copySelectedFiles`/`moveSelectedFiles` (build FSO list + dest, hand to
  `copyFiles`/`moveFiles` -> a progress dialog driving a `COperationPerformer`), `deleteFiles`/
  `deleteFilesIrrevocably`, `createFolder`/`createFile`.
- **Other commands:** `viewFile`/`editFile`/`quickViewCurrentFile`/`toggleQuickView`, `invertSelection`,
  `filterItemsByName`, `refresh`, `findFiles`, `showHiddenFiles`, `showAllFilesFromCurrentFolderAndBelow`,
  `openSettingsDialog`, `calculateStatistics`, `calculateEachFolderSize`, `checkForUpdates` (github auto
  updater), `about`, `toggleFullScreenMode`, `toggleTabletMode`, recycle-bin context menu.
- **Command line:** `executeCommand`, history recall (`selectPreviousCommandInTheCommandLine`,
  `_commandLineCompleter`), `pasteCurrentFileName`/`pasteCurrentFilePath`, `fileListReturnPressed`
  (Enter in the list with text in the command box runs the command).
- **Focus management:** `focusChanged`, `tabKeyPressed` (manual Tab between panels), `currentPanelChanged`.
- **Plugins:** `createToolMenuEntries` / `addToolMenuEntriesRecursively` materialize a plugin's
  `CPluginProxy::MenuTree` into the Commands menu.
- **Window title (#143):** `updateWindowTitleWithCurrentFolderNames`.
- **Background file-op dialogs:** registered in `_activeFileOperationDialogs`; `nextBackgroundDialogPosition`
  cascades them; `onFileDialogFinished` cleans up.
- A `QTimer _uiThreadTimer` -> `uiThreadTimerTick()` -> `CController::uiThreadTimerTick()` (drains UI queue,
  polls watchers/volumes). See [threading.md](threading.md).

## CPanelWidget (`src/panel/cpanelwidget.{h,cpp,ui}`) — one per side

`final : QWidget`, implements `IVolumeListObserver`, `PanelContentsChangedListener`,
`FileListReturnPressOrDoubleClickObserver`, `CursorPositionListener`. The visual panel: drive buttons,
path navigator, info label, the file list view, and **the tab bar**.

- **Single shared view, per-tab models.** The `.ui` provides one `CFileListView`. The widget keeps a
  `std::vector<PanelTab> _tabs`, index-aligned with the `QTabBar` and with the controller's tab list.
  `PanelTab = { CFileListModel*, CFileListSortFilterProxyModel*, QItemSelectionModel*, QByteArray headerState }`.
  `_model`/`_sortModel`/`_selectionModel` are just the **active** triplet's pointers, so the rest of the
  widget stays tab-agnostic. `activateTab(index)` swaps the active triplet into the shared view,
  saving/restoring that tab's own `headerState` (column widths/order/visibility — the sort indicator bits
  are ignored because the sort proxy owns the sort).
- **Tabs:** `createNewTab`, `closeCurrentTab` (no-op if last), `switchToNext/PreviousTab`,
  `openCurrentItemInNewTab` (Ctrl+Up), middle-click-folder (`onItemMiddleClicked`), `duplicateTab`,
  `closeAllOtherTabs`, `switchToTabByPosition` (Ctrl+1..9). Bar hidden while a single tab. Tab data stores
  the controller's tab **ID** (`tabIdAt`), so UI positions map to stable core ids. Drag-reorder
  (`onTabBarTabMoved`) mirrors into `_tabs` and the controller. See [tabs.md](tabs.md).
- **Selection/cursor:** Total Commander semantics (Space toggles + shows dir size, Insert-style region
  selection, shift-region). `selectedItemsHashes`, `currentItemHash`, `invertSelection`. Emits
  `itemActivated(hash, panel)` and `currentItemChangedSignal(panel, hash)`.
- **Clipboard / DnD:** copy/cut/paste via `OsShell`; `pasteImage` (saves clipboard image to a file).
- **Navigation UI:** drive buttons (`driveButtonClicked`), `toRoot`, favorites menu/editor, path history
  dropdown (`fillHistory`, `pathFromHistoryActivated`) fed by the controller's visited-locations.
- **Filter:** `showFilterEditor` / `CFileListFilterDialog` -> live name filter on the sort proxy.
- `savePanelState`/`restorePanelState` here are the **view header** (column) state, distinct from the
  controller's path/tab persistence.

## File list MVC (`src/panel/filelistwidget/`)

- **`CFileListView` (`cfilelistview.{h,cpp}`)** — `final : QTreeView` (flat, used as a multi-column list).
  Custom mouse/key handling for TC-style selection (drag-select, shift/ctrl, PgUp/PgDn region,
  single-click vs context menu). Uses an **observer list** (`FileListViewEventObserver`) rather than signals
  for Enter/double-click so the event stops at the first consumer:
  `FileListReturnPressedObserver` (Enter only, for command line) and
  `FileListReturnPressOrDoubleClickObserver` (activation) are the two convenience bases.
  Signals: `contextMenuRequested`, `ctrlEnterPressed`, `ctrlShiftEnterPressed`, `itemMiddleClicked`,
  `keyPressed`. `setHeaderAdjustmentRequired` / column auto-sizing.
- **`CFileListModel` (`model/cfilelistmodel.{h,cpp}`)** — `final : QAbstractItemModel`. Holds only
  `std::vector<qulonglong> _itemHashes`; **reads actual cell data live from `_controller.panel(p)`** (the
  active tab) in `data()`. Columns: `Name/Ext/Size/Date` (`columns.h`, `NumberOfColumns`). Custom role
  `FullNameRole`. Full **drag & drop** (`mimeData`/`dropMimeData`/`canDropMimeData`, file-url mime). Emits
  `itemEdited(hash, newName)` for inline rename.
- **`CFileListSortFilterProxyModel` (`model/cfilelistsortfilterproxymodel.{h,cpp}`)** — `final :
  QSortFilterProxyModel`. Natural sort via `CNaturalSorterQCollator` (from qtutils); `lessThan` keeps "`..`"
  and dirs ordered correctly; also reads through `_controller`. Emits `sorted`.
- **Consequence:** because both models query `panel(side)` = active tab, a tab's models are only valid while
  that tab is active. The design guarantees only the active triplet is ever attached to the view, so only it
  is queried. See [tabs.md](tabs.md) and [oddities.md](oddities.md).
- **`delegate/cfilelistitemdelegate`** — custom painting (icons, selection). **`cfocusframestyle`** — draws
  the active-panel focus frame. **`cfilelistfilterdialog`** — the quick name filter.

## CPanelDisplayController (`src/panel/cpaneldisplaycontroller.{h,cpp}`)

Thin bridge between a side and its `CPanelWidget`, managing the **quick-view** `QStackedWidget`: page 0 is
the panel, page 1 is a plugin viewer window. `startQuickView(WindowPtr<CPluginWindow>&&)` / `endQuickView`
/ `quickViewActive`. Owns the live quick-view window (custom-deleter `WindowPtr`, see [plugins.md](plugins.md)).

## Dialogs & windows

- **`progressdialogs/`** — `CCopyMoveDialog` (drives + observes a `COperationPerformer`),
  `CDeleteProgressDialog`, `CPromptDialog` (the file-exists / read-only / error prompt; returns a
  `UserResponse`), `CFileOperationConfirmationPrompt`, `progressdialoghelpers`. Cancellation confirmation
  pauses copy/move/delete work; declining restores the prior pause state, while confirming signals
  cancellation without first resuming filesystem mutations. Common base
  `CFileOperationDialogBase` (registered with `CMainWindow` for background cascading).
- **`settings/`** — 4 pages implementing a `SettingsPage` interface (from qtutils `CSettingsDialog`):
  `CSettingsPageInterface`, `CSettingsPageEdit`, `CSettingsPageOperations`, `CSettingsPageOther`.
- **`favoritelocationseditor/`** — `CFavoriteLocationsEditor` + `CNewFavoriteLocationDialog`.
- **`filessearchdialog/`** — `CFilesSearchWindow` (front-end for `CFileSearchEngine`; double-click a result
  navigates the panel to it).
- **`aboutdialog/`** — `CAboutDialog`. **`tools/CFileStatsWindow`** — folder statistics view.
- **`version.h`** — app version string.

## GUI tests (`qt-app/gui-tests/`)

Minimal/manual. `combobox/` is a standalone harness for the history combo box. Not part of CI.

See [core-engine.md](core-engine.md) for everything behind the controller, [tabs.md](tabs.md) for the
tab machinery that spans this layer and the core.
