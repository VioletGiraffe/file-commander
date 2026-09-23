# Qt GUI app (`qt-app/`)

The GUI is a Qt Widgets executable. `qt-app.pro` is its authoritative source list; Designer forms live beside their
owners. Filesystem and panel access goes through `CController`.

## Source map

| Area | Start here |
|------|------------|
| Startup and application settings | `src/main.cpp` |
| Commands, focus, shortcuts, ownership | `src/cmainwindow.{h,cpp,ui}` |
| Panel tabs and visible-side state | `src/panel/cpanelwidget.{h,cpp,ui}` |
| File-list view/model/delegate | `src/panel/filelistwidget/` |
| Quick view | `src/panel/cpaneldisplaycontroller.{h,cpp}` |
| Blocking shell operations: native delete, clipboard paste | `src/cshelloperationrunner.{h,cpp}` |
| Command-line output panes | `src/commandoutput/`; see [process-launching.md](process-launching.md) |
| Programs menu editor | `src/programseditor/`; see [process-launching.md](process-launching.md) |
| File-operation UI | `src/progressdialogs/` |
| Search, tools, and settings | `src/filessearchdialog/`, `src/tools/`, `src/settings/` |

## Main window

`main()` owns `CController` and `CPluginEngine`; `CMainWindow` borrows both and owns the panel widgets and their
display controllers. Its current/other panel pointers follow focus rather than fixed left/right position. It routes
commands, converts UI state into core requests, owns the resulting dialogs, and materializes plugin menus. A UI
timer drains controller and panel execution queues.

After the application event loop and native shell operations finish, `main()` explicitly shuts down the controller
while the window and plugin modules remain alive. Stack declaration order then destroys the window, plugin engine,
controller, shell runner, and application in that order. This releases plugin-defined windows before their modules.
During engine teardown, each plugin's proxy retires its tagged work before the plugin instance is destroyed and its
module is unloaded; the controller remains alive throughout that sequence.

## Panel widget and file-list MVC

Each side has one shared `CFileListView`. Every tab owns a `PanelTab` with its model, selection model, and saved
header state; the widget's unqualified model pointers alias the active tab's.

The tab bar, UI vector, and core tab list remain position-aligned, but cross-layer calls use the stable ID stored in
tab data. Positions must be resolved again immediately before close or reorder operations.

The widget and the main window implement the core listener interfaces; which callbacks each handles and on which
thread they arrive is in [notifications.md](notifications.md).

Only the tab on screen is refilled from its controller panel: background-tab notifications are filtered by stable ID,
and a background tab's model keeps its last rows until activation refills it. Tab activation swaps the models and
the view state. The Qt-specific activation ordering belongs in `CPanelWidget::activateTab()`, not here. See
[tabs.md](tabs.md).

Within one navigation (`CPanel::navigationId()`), a refill updates the model row by row: the selection, the cursor, an
open rename editor and the rows on screen stay put. A navigation, even to the folder in view, resets the model, as does
a refill changing too many rows for an update to pay off; the widget then places the cursor and restores the selection.

`CFileListView` owns orthodox selection and keyboard/mouse behavior. The model holds a copy of the rows it displays,
sorts and filters them, and depends on neither the controller nor the OS shell; delegates own painting. Drag/drop
uses the same operation launch boundary as commands.

## File-operation UI

`progressdialogs/fileoperationlaunch` converts selected paths and destination text into a typed core request and
chooses the deletion backend. It decides `DestinationIntent` before path parsing, because a trailing separator is
user intent even when the directory does not yet exist.

`CFileOperationDialog` owns one `CFileOperationJob`, drains typed events, presents only the decisions offered by the
core, and formats the outcome. It may remain hidden for quick success, appear for a decision or background
operation, and keep a completed summary visible only when it needs attention. Closing an active dialog suppresses
later display but does not weaken job/listener lifetime requirements.

## Quick view and plugins

`CPanelDisplayController` switches between the normal panel and a plugin window. It retains the plugin-defined
custom-deleter pointer so destruction occurs in the module that allocated the window. See [plugins.md](plugins.md).
