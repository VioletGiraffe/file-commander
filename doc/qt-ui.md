# Qt GUI app (`qt-app/`)

The GUI is a Qt Widgets executable. `qt-app.pro` is its authoritative source list; Designer forms live beside their
owners. Filesystem and panel access goes through `CController`.

## Source map

| Area | Start here |
|------|------------|
| Startup and application settings | `src/main.cpp` |
| Commands, focus, shortcuts, ownership | `src/cmainwindow.{h,cpp,ui}` |
| Function key buttons (F3 to F8) | `src/functionkeybar/` |
| Panel tabs and visible-side state | `src/panel/cpanelwidget.{h,cpp,ui}` |
| File-list view/model/delegate | `src/panel/filelistwidget/` |
| Quick view | `src/panel/cpaneldisplaycontroller.{h,cpp}` |
| Viewer windows (F3) | `src/viewerwindow/` |
| Blocking shell operations: native delete, clipboard paste | `file-operations/shelloperations/` |
| Command-line output panes | `src/commandoutput/`; see [process-launching.md](process-launching.md) |
| Programs menu editor | `src/programseditor/`; see [process-launching.md](process-launching.md) |
| File-operation UI | `src/progressdialogs/` (launch, confirmation); the dialogs are in `file-operations/fileoperationsui/` |
| Search, tools, and settings | `src/filessearchdialog/`, `src/tools/`, `src/settings/` |
| File and volume icons | `src/iconprovider/`; the app's folder and file glyphs replace OS icons that show only the type |
| Themes | `src/themes/`; the app-independent parts are in `qtutils/theme/` |

## Main window

`main()` owns `CController`, `CPluginEngine` and `CIconProvider`; `CMainWindow` borrows them and owns the panel
widgets and their display controllers. Its current/other panel pointers follow focus rather than fixed left/right
position. It routes commands, converts UI state into core requests, owns the resulting dialogs, and materializes plugin
menus. A UI timer drains the controller, panel and icon provider execution queues.

After the application event loop and native shell operations finish, `main()` explicitly shuts down the controller
while the window and plugin modules remain alive. Stack declaration order then destroys the window, icon provider,
plugin engine, controller, shell runner, and application in that order. The main window destroys the viewer windows
still open, so no plugin-made widget outlives the engine.
During engine teardown, each plugin's proxy retires its tagged work before the plugin instance is destroyed; the
controller remains alive throughout that sequence.

## Themes

The app always runs one of its own themes on the Fusion style. qtutils' `CThemeController` stores the colour scheme and
one theme name per polarity and announces changes; `installTheme()` applies the selected theme's palette, font and
stylesheet. A theme's structure, rounded or flat, selects the stylesheet's sections, the form of the function keys and
the panels' frame and row order: a flat panel draws its own frame, with the path row in its top edge.
`--widget-gallery` on the command line also opens qtutils' widget gallery, a window of every stock control, for
checking a theme's coverage.

The application palette is the only part of a theme that reaches a plugin. qtutils is linked statically into the app
and into every plugin, so `CThemeController::instance()` in a plugin is a separate object that the app's choices never
reach. Plugin code reads its widget's palette. App code reads `appliedTheme()` for what neither the palette nor the
stylesheet carries: layout geometry, the file list's shape, a colour without a palette role such as error text.

On every stylesheet change Qt restores the font each widget had when it was first polished. A widget given a font
later, such as the file list, sets it again whenever the theme is applied: on `themeChanged` and on a tablet mode switch.
Tablet mode enlarges the application font, which the theme module owns.

Deferred styling work is in [styling-todo.md](styling-todo.md).

## Panel widget and file-list MVC

Each side has one shared `CFileListView`. Every tab owns a `PanelTab` with its model, selection model, and saved
column layout; the widget's unqualified model pointers alias the active tab's.

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
a refill changing too many rows for an update to pay off; the widget then places the cursor.
A cursor item the core designates (a created or renamed entry) is usually not listed yet: the tab holds it as a target
until a refill lists it, a navigation starts, or the cursor is moved.

`CFileListView` owns orthodox selection gestures and keyboard/mouse behavior. The model holds a copy of the rows it
displays, sorts and filters them by the core's rules (`file-commander-core/src/filelist/`) with the tab's sorting and
name filter, and depends on neither the controller nor the OS shell. The view paints the column separators, the
selection and the cursor behind each row; the delegate paints the cells and defines the row geometry. Both take their
colours from the widget palette and the rows' shape from a `FileListLook`, which the panel passes on from the theme.
Drag/drop uses the same operation launch boundary as commands.

The selection belongs to the tab's `CPanel`, and the selection model shows a copy. Gestures push their changes into
the core from `selectionChanged`. After every refill and every change made through the core, the widget copies the
core's selection back: Qt changes a selection silently on a layout change or a reset, so the copy never relies on it.

## File-operation UI

The main window launches operations through the core's `fileoperationlaunch` (see [core-engine.md](core-engine.md)).
The dialogs it opens are described in `file-operations/doc/architecture.md`.

## Viewers

A viewer plugin supplies a `CFileViewer` widget; the app supplies the frame around it. `CViewerWindow` is the F3 frame.
`CPanelDisplayController` is the quick-view frame: it swaps the viewer into the other panel's stack and, while the
cursor stays on files of the same viewer plugin, loads each file into the viewer already there. `CMainWindow` tracks
both kinds of frame so `closeViewedFilesWithin()` reaches every open viewer. See [plugins.md](plugins.md).
