# Terminal UI (`tui/`)

`fcterm` is a second front end over the core, built on Turbo Vision (the `tvision` submodule). It links QtCore only
and runs in any terminal, SSH sessions included. It drives the same `CController` as the Qt app. Logic both front
ends need lives in the core, which returns the text to show and the program to run.

## Source map

| Area | Start here |
|------|------------|
| Startup, settings, log file | `app/src/main_tui.cpp` |
| Event loop, the two bottom rows, running a program | `app/src/ctuiapplication.{h,cpp}` |
| One side: its listing, keys and commands | `app/src/ctuipanel.{h,cpp}` |
| The rows, their columns and the column header | `app/src/ctuifilelist.{h,cpp}` |
| A list's cursor, scrolling, keys and mouse | `app/src/ctuirowlist.{h,cpp}` |
| Dialogs | `app/src/ctuidialogs.{h,cpp}` |
| A dialog's fields, and the histories they share with the GUI | `app/src/ctuifields.{h,cpp}` |
| Batch rename | `app/src/ctuibatchrenamedialog.{h,cpp}` |
| Colours | `app/src/ctuipalette.{h,cpp}` |
| Copy, move, delete | `app/src/ctuifileoperations.{h,cpp}` |
| Find files | `app/src/ctuifinddialog.{h,cpp}` |
| The pager, the editor, the shell, opening a document | `app/src/ctuiprograms.{h,cpp}` |
| Turbo Vision as a static library | `tvision/tvision.pro` |
| Tests | `tests/`; see [testing.md](testing.md) |

## Event loop

One thread. Turbo Vision owns the loop and no Qt event loop runs: `CTuiApplication::getEvent` delivers Qt's events and
the controller's UI queue before every Turbo Vision event, so the core's notifications also arrive while a modal
dialog is open.

## Panels

- At start, the left side shows the folder argument, or else the working folder: in the tab already showing it, else in
  the active tab (`CController::showFolderInItsTabOrActiveTab`). The GUI's `--tui` handover passes the argument on.
- Each side is a `CTuiPanel` showing the side's active tab. Switching tabs refills it from that tab's `CPanel`; a tab
  strip shows while the side has more than one tab. [tabs.md](tabs.md) has the core's side.
- The rows are a copy of `CPanel::visibleItems()`. Sorting, the name filter and the selection belong to the `CPanel`,
  as in the Qt UI; the list shows a copy of the selection, refreshed after every change made through the core.
- `CTuiFileList` builds on `CTuiRowList`, a plain `TView` with a cursor, not on `TListViewer`: that counts rows in
  `short`, and a folder can hold more.
- The columns are Name, Ext, Size and Date, as many as the panel's width fits. Name and the sorted column always stay.
  `fileListColumns` makes this choice for the rows and for the header above them, so layout decisions by width go
  there. A click on a header title sorts by that column.

## Colours

- Every colour is a role in Turbo Vision's palette chain: a view's palette entry indexes its owner's palette, up to the
  application palette, which holds the colours. `ctuipalette.cpp` defines that palette in RGB. Turbo Vision's stock
  colours are BIOS indices into the terminal's 16-colour palette, which colour schemes redefine.
- The colours are in the xterm 256-colour cube, so they look the same on 256-colour and true-colour terminals. Terminals
  with fewer colours are not targeted.
- The panel's own roles follow Turbo Vision's entries in the application palette; `CTuiPanel`'s palette maps them.
  New panel roles go into `PanelPaletteEntry` and its colour table.

## Commands

- Every panel action is a command, carried out in `CTuiPanel::runCommand`. The menus (`CTuiApplication::initMenuBar`)
  bind the keys, and see them before the focused view does.
  - Exceptions: the panel binds the keys a text field uses, so that the filter line still receives them. The application
    binds Tab, which switches panels.
  - The status line binds F10, which opens the menus, and Alt+1 to Alt+9, which share one menu item. Its other items
    and the hint row are labels.
- Copy, move and delete build their requests with the core's `fileoperationlaunch` and run on the same engine as the
  Qt app; [core-engine.md](core-engine.md) has the deletion backends. The progress dialog opens only for an operation
  that outlasts a short delay or asks a question. A trash that refuses items offers them for permanent deletion.
- Find files (Alt+F7) runs the core's `CFileSearchEngine` with the GUI's fields, options and settings keys. The dialog
  takes the results from a `CFileSearchResultsBuffer` in its `getEvent`, at most every 100 ms. The application keeps
  the dialog, so its last results show again; closing it stops a running search.
- Batch rename (Shift+F2; the GUI's Ctrl+M is Enter in a terminal) renders the core's `CBatchRenameSession`. Turbo Vision
  reports no edit, so the dialog compares the rules after every event. The renaming runs on the UI thread, which reads only
  Esc meanwhile.
- A field's history is shared with the GUI's field through its settings key. Turbo Vision keeps every field's history in
  one block, which `main` enlarges before the application allocates it.
- Viewing, editing, opening a document and running a program: see [process-launching.md](process-launching.md).
- `CTuiApplication::copyToClipboard` sends a copy over SSH to the terminal (OSC 52), to reach the clipboard of the
  computer the user types on. Elsewhere Turbo Vision chooses, trying the local clipboard tools first. Neither path
  reports whether the copy arrived.

## Settings and logging

- The terminal UI shares the GUI's settings, tabs included: see [persistence.md](persistence.md).
- `--settings-dir <folder>` keeps the settings in an INI file under that folder; the tests use it.
- Qt's messages go to `file-commander-tui.log` in the temporary folder: stderr would draw over the screen.

## Terminal input

- Turbo Vision turns on win32-input-mode, which breaks keys over SSH from Windows Terminal. `disableWin32InputMode`
  turns it off at start and in `CTuiApplication::resume()`.
- Turbo Vision takes a fast burst of text keys for a paste, and an Enter arriving right behind it is lost. Scripted
  input types one key at a time.

## Build and packaging

- `tvision/tvision.pro` builds the submodule with the include paths and defines of its own CMake build. Unix needs
  ncursesw; macOS links the system ncurses.
- `fcterm` ships beside the GUI executable: in the Windows installer, in the macOS app bundle and in the Linux
  AppImage. On Linux it also ships alone, as `fcterm.AppImage` (`fcterm.arm64.AppImage` on ARM), with QtCore only.
  `CI.yml` holds the packaging steps.
- On Linux and macOS, the GUI executable hands over to `fcterm` for `--tui`, and on its own where it has no screen: Linux
  without a display, macOS over SSH (`terminalUiRequested` in `qt-app/src/main.cpp`). Windows has no handover: a GUI
  program cannot run in the console it was started from. In the AppImage, `installer/linux/AppRun` makes the same
  choice first, since the GUI executable cannot start without the system's X11 and GL libraries.
