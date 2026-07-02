# Persistence & settings

Backed by `CSettings` (qtutils wrapper over `QSettings`). App/org names ("File Commander"/"GitHubSoft", set
in `main.cpp`) determine the store location (Windows registry / ini per platform). All keys are `#define`d
in `file-commander-core/include/settings.h` (the only public core header). `QSL(...)` is a static `QString`
literal helper.

## Key map (`settings.h`)

**Internal (session state, not user-facing):**

| Key macro | Value | Meaning |
|-----------|-------|---------|
| `KEY_LPANEL_PATH` / `KEY_RPANEL_PATH` | `Internal/Core/{L,R}Panel/Path` | Active tab's path (also kept for back-compat with pre-tabs installs). |
| `KEY_LPANEL_TABS` / `KEY_RPANEL_TABS` | `.../{L,R}Panel/Tabs` | List of all tab paths for the side. |
| `KEY_LPANEL_ACTIVE_TAB` / `KEY_RPANEL_ACTIVE_TAB` | `.../{L,R}Panel/ActiveTab` | Index of the active tab. |
| `KEY_LPANEL_TAB_CURSORS` / `KEY_RPANEL_TAB_CURSORS` | `.../{L,R}Panel/TabCursors` | Per-tab cursor item **hashes**, parallel to the Tabs list. Hash is path-derived so it survives restart; missing/stale -> first item. |
| `KEY_HISTORY_L` / `KEY_HISTORY_R` | `.../{L,R}Panel/History` | Active tab's back/forward history (only the active tab's is persisted). |
| `KEY_LPANEL_VISITED_LOCATIONS` / `KEY_RPANEL_VISITED_LOCATIONS` | `.../{L,R}Panel/VisitedLocations` | Per-side, tab-independent visited-folders log (survives tab close). |
| `KEY_LAST_PATH_FOR_DRIVE_L/R` | `.../{L,R}Panel/LastPathForDrive%1` | Remembered folder per drive letter, per side. |
| `KEY_FAVORITES` | `Internal/Core/Favorites` | Favorites tree. |
| `KEY_LAST_COMMANDS_EXECUTED` | `Internal/Interface/LastCommandsExecuted` | Command-line history. |
| `KEY_PROMPT_DIALOG_GEOMETRY` | `Internal/Interface/PropmptDialog/Geometry` | File-op prompt geometry. (Note the typo "Propmpt" in the key string — keep it; changing it orphans saved geometry.) |
| `KEY_LAST_UPDATE_CHECK_TIMESTAMP` | `.../Update/LastUpdateCheckTimestamp` | Auto-update throttle. |

**User-facing (main UI + Settings dialog):**

| Key macro | Value | Page |
|-----------|-------|------|
| `KEY_INTERFACE_SHOW_HIDDEN_FILES` | `Interface/View/ShowHiddenFiles` | main UI toggle |
| `KEY_INTERFACE_RESPECT_LAST_CURSOR_POS` | `Interface/Selection/RespectLastCursorPosition` | Interface |
| `KEY_INTERFACE_FILE_LIST_FONT` (`INTERFACE_FILE_LIST_FONT_DEFAULT` = Roboto Mono 10 Light) | `Interface/View/FileListFont` | Interface |
| `KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS` | `Interface/View/ShowSpecialFolderIcons` | Interface |
| `KEY_INTERFACE_STYLE_SHEET` | `Interface/Style/StylesheetText` | Interface |
| `KEY_INTERFACE_COLOR_SCHEME` | `Interface/ColorScheme` | Interface (Qt 6.8+ light/dark) |
| `KEY_INTERFACE_STYLE` | `Interface/Style` | Interface (QStyle name) |
| `KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION` | `Operations/CopyMove/AskForConfirmation` | Operations |
| `KEY_EDITOR_PATH` | `Edit/EditorProgramPath` | Edit |
| `KEY_OTHER_SHELL_COMMAND_NAME` | `Other/Shell/ShellCommandName` | Other |
| `KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY` | `Other/UpdateChecking/CheckAutomatically` | Other |

## Save/restore flow (centralized in `CController`)

Persistence moved fully into `CController`; `CPanel` no longer reads/writes settings (it only seeds history
via `restoreHistory(vector)`).

- **Restore (`restorePanelState(p)`, at startup):** read `KEY_*PANEL_TABS`; for each path create a tab; set
  active tab from `KEY_*PANEL_ACTIVE_TAB`; seed per-tab cursor from `KEY_*PANEL_TAB_CURSORS`; seed the active
  tab's history from `KEY_HISTORY_*`. **Migration:** an old install with only `KEY_*PANEL_PATH` becomes a
  single tab.
- **Save (`savePanelState(p)`):** write all tab paths + active index + per-tab cursors + mirror active path
  to `KEY_*PANEL_PATH`. **Deduped** against `_lastSavedTabSignature[side]` so the frequent watcher-driven
  refreshes don't rewrite settings every tick. Driven by `onPanelContentsChanged` (controller listens to its
  own tabs) and on shutdown (dtor).
- **History/visited (`saveHistoryList(p)`):** writes the active tab's back/forward history + the side's
  visited-locations log. **Shutdown only** (it's churny; not worth writing on every navigation).
- **Visited-locations** are appended live in `onCurrentPathChanged` (fires only on an actual directory
  change, not every refresh).

## Other persisted bits (outside `settings.h`)

- **View header state** (column widths/order/visibility) — per tab, held in `PanelTab::headerState` in the
  UI; `CPanelWidget::savePanelState`/`restorePanelState` handle the `QByteArray`. Distinct from the
  controller's path/tab persistence above.
- **Window geometry / splitter** — standard `QMainWindow`/widget state save.

See [tabs.md](tabs.md) for how tab identity maps onto these keys.
