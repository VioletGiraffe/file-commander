# Qt GUI app (`qt-app/`)

The GUI is a Qt Widgets executable. `qt-app.pro` is its authoritative source list; Designer forms live beside their
owners. Filesystem and panel access goes through `CController`.

## Source map

| Area | Start here |
|------|------------|
| Startup and application settings | `src/main.cpp` |
| Commands, focus, shortcuts, ownership | `src/cmainwindow.{h,cpp,ui}` |
| Panel tabs and visible-side state | `src/panel/cpanelwidget.{h,cpp,ui}` |
| File-list view/model/proxy/delegate | `src/panel/filelistwidget/` |
| Quick view | `src/panel/cpaneldisplaycontroller.{h,cpp}` |
| File-operation UI | `src/progressdialogs/` |
| Search, tools, and settings | `src/filessearchdialog/`, `src/tools/`, `src/settings/` |

## Main window

`CMainWindow` owns `CController`, both panel widgets, and their display controllers. Its current/other panel pointers
follow focus rather than fixed left/right position. It routes commands, converts UI state into core requests, owns
the resulting dialogs, and materializes plugin menus. A UI timer drains controller and panel execution queues.

## Panel widget and file-list MVC

Each side has one shared `CFileListView`. Every tab owns a `PanelTab` with its model, sort/filter proxy, selection
model, and saved header state; the widget's unqualified model pointers alias the active triplet.

The tab bar, UI vector, and core tab list remain position-aligned, but cross-layer calls use the stable ID stored in
tab data. Positions must be resolved again immediately before close or reorder operations.

Both list models resolve items through the active controller panel. Therefore only the active triplet may be
queried or attached to the view, tab activation swaps the entire triplet and its view state, and background-tab
notifications must be filtered by stable ID. The Qt-specific activation ordering belongs in
`CPanelWidget::activateTab()`, not here. See [tabs.md](tabs.md).

`CFileListView` owns orthodox selection and keyboard/mouse behavior. The model stores item hashes and resolves cell
data from the panel; the proxy owns sorting and filtering; delegates own painting. Drag/drop uses the same operation
launch boundary as commands.

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
