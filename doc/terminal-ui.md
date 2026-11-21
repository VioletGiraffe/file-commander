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
| Colours: Turbo Vision's palettes | `app/src/ctuipalette.{h,cpp}` |
| Colours: the themes | `app/src/ctuicolourtheme.{h,cpp}` |
| Copy, move, delete | `app/src/ctuifileoperations.{h,cpp}` |
| Find files | `app/src/ctuifinddialog.{h,cpp}` |
| Favorites | `app/src/ctuifavoritesdialog.{h,cpp}` |
| Folder comparison | `app/src/ctuifoldercomparison.{h,cpp}` |
| The viewer and quick view | `app/src/ctuiviewer.{h,cpp}` |
| The editor, the shell, opening a document | `app/src/ctuiprograms.{h,cpp}` |
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
  strip shows while the side has more than one tab. It wraps onto up to three rows, past which it scrolls by row to
  keep the active tab in view, or by the mouse wheel; a mark in its last column shows hidden rows. [tabs.md](tabs.md) has the core's side.
- The rows are a copy of `CPanel::committedListing()` in a `SortedFileList`. A refresh of the same view updates it in
  place, so unchanged rows keep their sort keys. Sorting, the name filter and the selection belong to the `CPanel`, as in
  the Qt UI; the list shows a copy of the selection, refreshed after every change made through the core.
- When the core withdraws a list, the panel empties its rows at once but keeps the old frame on screen briefly, so a
  listing that arrives in time replaces it with no blank frame.
- `CTuiFileList` builds on `CTuiRowList`, a plain `TView` with a cursor, not on `TListViewer`: that counts rows in
  `short`, and a folder can hold more.
- The columns are Name, Ext, Size and Date, as many as the panel's width fits. Name and the sorted column always stay.
  `fileListColumns` makes this choice for the rows and for the header above them, so layout decisions by width go
  there. A click on a header title sorts by that column.

## Colours

- Every colour is a role in Turbo Vision's palette chain: a view's palette entry indexes its owner's palette, up to the
  application palette, which holds the colours. `ctuipalette.cpp` defines that palette in RGB. Turbo Vision's stock
  colours are BIOS indices into the terminal's 16-colour palette, which colour schemes redefine.
- The colours are in the xterm 256-colour cube or grey ramp, so they look the same on 256-colour and true-colour
  terminals. Exceptions are marked in `ctuicolourtheme.cpp`: single off-palette colours, and true-colour themes. Turbo Vision
  rounds them to the nearest palette colour on a 256-colour terminal. Terminals with fewer colours are not targeted.
- Turbo Vision uses true colour only when `COLORTERM` is `truecolor` or `24bit`. SSH does not pass it on by default:
  `SetEnv COLORTERM=truecolor` in the client's SSH config does, where the server's `AcceptEnv` allows it.
- The panel's own roles follow Turbo Vision's entries in the application palette; `CTuiPanel`'s palette maps them.
  New panel roles go into `PanelPaletteEntry` and its colour table.
- The hint row and the key bar have their own roles after the panel's: Turbo Vision's status line shares the menus' roles.
- A colour theme's `ThemeParameters` hold one colour per kind of thing on screen. `applicationPaletteFor` pairs them
  into the application palette's entries, so a new palette entry is written once for every theme. View > Theme lists
  the themes and shows the one under the cursor on the whole screen; the choice is the terminal UI's own setting.
- A theme can put the file list's rows on bands of alternating background, of a height it chooses. It can also give
  the column header a background and uppercase titles, and the status line a divider line and a colour for its values.

## Commands

- Every panel action is a command, carried out in `CTuiPanel::runCommand`. The menus (`CTuiApplication::initMenuBar`)
  bind the keys, and see them before the focused view does.
  - Exceptions: the panel binds the keys a text field uses, so that the filter line still receives them. The application
    binds Tab, which switches panels.
  - The key bar binds F10, which opens the menus, and Alt+1 to Alt+9, which share one menu item. Its other items run
    their commands on a click only. The hint row binds no keys.
- Copy, move and delete build their requests with the core's `fileoperationlaunch` and run on the same engine as the
  Qt app; [core-engine.md](core-engine.md) has the deletion backends. The progress dialog opens only for an operation
  that outlasts a short delay, counted again from each answered question, or that has work left after one. A trash
  that refuses items offers them for permanent deletion.
- Find files (Alt+F7) runs the core's `CFileSearchEngine` with the GUI's fields, options and settings keys. The dialog
  takes the results from a `CFileSearchResultsBuffer` in its `getEvent`, at most every 100 ms, into a `SortedFileList`
  it draws with the panel's columns in its own colours: `CTuiFileList` takes its colours from the panel window's palette.
  The results start in arrival order, with the cursor following the newest one until the user moves it elsewhere. A click
  on a column title or the panel's sort keys (Ctrl+F3 to Ctrl+F6) take a column through ascending, descending and arrival
  order. The end of a search moves the focus to the results unless the user acted during it. The application keeps the
  dialog, so its last results show again; closing it stops a running search.
- Favorites (Ctrl+D) and the folder history (Alt+F12) are the GUI's: the core's `CFavoriteLocations` and
  `CController::recentFolders`.
- File comparison is the GUI's: the core's `filesToCompare` and `fileComparisonResultMessage` pick the files and word
  the messages, and `CFileComparator` compares them behind `waitWithProgressDialog`.
- Folder comparison runs the core's `CFolderComparison` on the two panels' folders. The application keeps the last
  result, and the command lists it again while each panel shows its compared folder or one below it.
- Occupied space (Ctrl+L) runs the core's `CFileStatisticsScan`, behind the progress dialog that folder comparison
  also uses (`waitWithProgressDialog`).
- Batch rename (Shift+F2: the GUI's Ctrl+M is Enter on many terminals, see Terminal input) renders the core's
  `CBatchRenameSession`. Turbo Vision reports no edit, so the dialog compares the rules after every event. The renaming
  runs on the core's `CBatchRenameRun`, behind `waitWithProgressDialog`; cancelling pauses it and asks first.
- A dialog button that Enter presses, or that does what Esc does, has no hot key (`DialogSpec::defaultButton`,
  `DialogSpec::escapeButton`).
- A field's history is shared with the GUI's field through its settings key. Turbo Vision keeps every field's history in
  one block, which `main` enlarges before the application allocates it.
- The viewer (F3) reads the whole file, as the GUI's text viewer does. It decodes with `QStringDecoder` alone, so a
  legacy code page other than the system's is not detected. Its keys, find options and find history are the GUI text viewer's.
- Quick view (Ctrl+Q) is a window over the other panel, showing the file under the active panel's cursor. Neither the
  window nor the panel under it takes the focus, so it scrolls by the mouse only. It reads at most 20 MiB of a file.
- The hint row and the key bar show the items of the top view's help context: the viewer's while it is open.
- Editing, opening a document and running a program: see [process-launching.md](process-launching.md).
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
- Turbo Vision takes a burst of text keys for a paste, Enter and Tab included, and an input line inserts an Enter or
  Tab inside a paste as a space. A paste ends at the first key that is not text, so scripted input follows its text
  with End.
- Which Ctrl combinations arrive depends on how the terminal encodes keys:
  - The Windows console: all of them, decoded from the scan code. Ctrl+Alt is lost on layouts with AltGr.
  - A terminal with an extended keyboard protocol that Turbo Vision requests at start (XTerm's modifyOtherKeys, Kitty's,
    far2l's): all of them.
  - Any other terminal sends control bytes. Ctrl+H, Ctrl+I, Ctrl+J, Ctrl+M and Ctrl+[ arrive as Backspace, Tab, Enter,
    Enter and Esc; Ctrl+Shift+letter as Ctrl+letter; Ctrl+digit and most Ctrl+punctuation not at all.
- Ctrl+C, Ctrl+Z, Ctrl+S and Ctrl+Q arrive as keys: Turbo Vision turns off the terminal's handling of them.
- Ctrl+C acts as Esc in a modal view and in the filter line. Elsewhere it exits.
- The terminal emulator's own shortcuts never arrive, such as Windows Terminal's Ctrl+Tab and Ctrl+V. Inside tmux, its
  prefix key (Ctrl+B by default) does not arrive either.
- Ctrl+letter other than H, I, J and M therefore arrives on every terminal, unless the terminal emulator binds it.

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
