# Terminal UI (`tui/`)

`fcterm` is a second front end over the core, built on Turbo Vision (the `tvision` submodule). It links QtCore only
and runs in any terminal, SSH sessions included. It drives the same `CController` as the Qt app. Logic both front
ends need lives in the core, which returns the text to show and the program to run.

## Source map

| Area | Start here |
|------|------------|
| Startup, settings, log file | `app/src/main.cpp` |
| Event loop, the two bottom rows, running a program | `app/src/ctuiapplication.{h,cpp}` |
| One side: its listing, keys and commands | `app/src/ctuipanel.{h,cpp}` |
| The rows and the cursor | `app/src/ctuifilelist.{h,cpp}` |
| Dialogs | `app/src/ctuidialogs.{h,cpp}` |
| Copy, move, delete | `app/src/ctuifileoperations.{h,cpp}` |
| Turbo Vision as a static library | `tvision/tvision.pro` |
| Tests | `tests/`; see [testing.md](testing.md) |

## Event loop

One thread. Turbo Vision owns the loop and no Qt event loop runs: `CTuiApplication::getEvent` delivers Qt's events and
the controller's UI queue before every Turbo Vision event, so the core's notifications also arrive while a modal
dialog is open.

## Panels

- Each side is a `CTuiPanel` showing the side's active tab. There are no tab commands.
- The rows are a copy of `CPanel::visibleItems()`. Sorting, the name filter and the selection belong to the `CPanel`,
  as in the Qt UI; the list shows a copy of the selection, refreshed after every change made through the core.
- `CTuiFileList` is a plain `TView`, not a `TListViewer`: that counts rows in `short`, and a folder can hold more.

## Commands

- Keys are bound in three places: the status line (the function keys, as commands for the focused panel),
  `CTuiApplication::handleEvent` (keys that act across the panels) and `CTuiPanel::handleEvent`. The hint row above
  the status line binds nothing.
- Copy, move and delete build their requests with the core's `fileoperationlaunch` and run on the same engine as the
  Qt app; [core-engine.md](core-engine.md) has the deletion backends. The progress dialog opens only for an operation
  that outlasts a short delay or asks a question. A trash that refuses items offers them for permanent deletion.
- Viewing, editing, opening a document and running a program: see [process-launching.md](process-launching.md).
- `CTuiApplication::copyToClipboard` sends a copy over SSH to the terminal (OSC 52), to reach the clipboard of the
  computer the user types on. Elsewhere Turbo Vision chooses, trying the local clipboard tools first. Neither path
  reports whether the copy arrived.

## Settings and logging

- The organization and application names differ from the GUI's, so the terminal UI's settings, saved tabs included,
  never overwrite the GUI's.
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
- `fcterm` ships in the Windows installer, in the macOS app bundle beside the GUI executable, and on Linux as its own
  AppImage, `fcterm.AppImage` (`fcterm.arm64.AppImage` on ARM). `CI.yml` holds the packaging steps.
