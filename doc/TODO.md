# TODO / backlog

Approved-but-not-started work, consciously deferred items, and known defects. Current state only; done items get
removed.

## Tabs: deferred nice-to-haves

- Locked tabs.
- Drag files from the list onto a tab header to copy/move into that tab's folder without switching to it.
- Persist back/forward history for inactive tabs.
- Make initial column sizing per-tab; the current fresh-install glitch is rare and self-correcting.

## File operations

- Conflict planning inspects each item under a merged destination folder individually (one to three stats per
  item, repeated at execution). One directory listing per merged folder would replace the per-item stats.
- Automatic bulk renaming in the conflict plan (`name (2).ext`), also the natural answer for same-name roots
  selected from a flattened view, which planning leaves to a live prompt today.

## Folder synchronization

Folder comparison exists (`file-operations/foldercomparison/`, `qt-app/src/tools/cfoldercomparisonwindow`);
synchronization does not. It is a core feature, not a plugin (see [plugins.md](plugins.md)): it builds a plan, a list
of copies, moves and deletions, and hands it to the file-operation engine, which already runs batches with progress
and conflict prompts.

Open decisions, to settle before building:
- Default comparison criteria: size, modification time or content. Modification times are unreliable across
  filesystems; content is authoritative but slow.
- Direction: mirror one side to the other, newer wins, or per row. A mirror deletes the entries only the destination
  has, which needs guarding.
- UI: an aligned two-column view with a direction and a checkbox per row, or a flat list of differences.

## Terminal UI: colours after running a program, in Windows Terminal

After fcterm runs a program (pager, editor, shell, Enter on a program) in Windows Terminal, the first cursor move
repaints the whole screen in the terminal's 16-colour palette. Cells redrawn later come back in their own colours.

- Cause: Windows Terminal's ConPTY (seen with 1.22; reproduced with 1.24 and 1.25). It shows a console screen-buffer
  switch as `ESC[?1049l` and a full repaint, and Turbo Vision switches buffers on every suspend and resume.
  - After the switch back, the first arrow, Home, End or Tab key arms one more repaint, in 16 colours, sent with the next
    write.
  - F12 and letters do not arm it.
  - The classic console window and the inbox ConPTY are unaffected.
- No app-side redraw can target it: fcterm cannot tell which input armed the repaint.
- Fix candidates:
  - Turbo Vision on the VT alternate screen instead of console screen buffers; this needs a `tvision` fork or an
    upstream change.
  - A report to microsoft/terminal.
- Reproducing:
  - The `Microsoft.Windows.Console.ConPTY` NuGet package holds `conpty.dll` and `OpenConsole.exe`.
  - `CTerminalSession` can call that DLL's `ConptyCreatePseudoConsole` and `ConptyClosePseudoConsole` instead of the
    system's.
  - Logging the raw output then shows the repaint after Ctrl+O, `exit` and Down.

## Visited-locations merge

Its design has open questions for the maintainer to settle; not to be changed until then.
