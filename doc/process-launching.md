# Process launching

Every way the app starts another process, how the command line runs commands and shows their output, and what
happens to running commands when the app exits.

## Entry points

| Trigger | Start here | Mechanism |
|---|---|---|
| Activating an executable in a panel | `CController::itemActivated` | `OsShell::runExecutable`: `ShellExecuteExW` on Windows, `QProcess::startDetached` elsewhere |
| Activating any other file | `CController::itemActivated` | `QDesktopServices::openUrl`, the OS file association |
| Edit (F4) | `CMainWindow::editFile` | `QProcess::startDetached` with the configured editor; `open -a` on macOS |
| Open terminal | `CController::openTerminal` | `OsShell::shellExecutable` picks the terminal; elevated through `OsShell::runExe` on Windows |
| Command line | `CMainWindow::executeCommand` | `OsShell::runExecutable` for a GUI program (`OsShell::guiProgramInvocation`), else `CCommandOutputArea::run` |

Paths embedded in a shell command line go through `shellQuotedPath`: it quotes only where cmd or sh would misread the
path, so the same call serves the clipboard. A `%VAR%` in a path still expands under cmd, even quoted. The terminal
launch's PowerShell and `osascript` branches need other quoting dialects; see [TODO.md](TODO.md).

## Command line

### GUI programs bypass the shell (Windows)

`cmd /c` waits for a GUI program to exit. Run through the shell, `notepad file.txt` would hold an empty output pane open,
be listed as running when the app exits, and be killed by Terminate.

`OsShell::guiProgramInvocation` lets a line launch directly when all of these hold:

- No shell syntax: `%` anywhere, or `& | < > ^ ( )` outside quotes, sends the line to the shell.
- The program resolves in cmd's search order: the working directory, then `PATH`, trying the `PATHEXT` extensions.
- `SHGetFileInfo(SHGFI_EXETYPE)` reports a GUI executable.

A check that cannot decide also sends the line to the shell. The arguments are passed on as typed: the program parses its
own command line. POSIX has no equivalent: `sh` returns at once for `open` and for `&`, and nothing marks a program as GUI.

### Everything else runs through the shell

`CShellCommand` runs `cmd.exe /s /c "pushd "<dir>" && <command>"`, or `/bin/sh -c <command>` in the working directory.

- `pushd`, not the process working directory: cmd does not support a UNC current directory, and `pushd` maps a
  temporary drive letter. `&&` keeps the command from running when the folder is gone.
- `/s` strips only the outermost quotes, so the directory can be quoted and a folder name may contain `&`.
- Qt passes `CREATE_NO_WINDOW` whenever the parent has no console, so no console window appears.

### Output

stdout and stderr share one pipe, which preserves their interleaving. A child writing into a pipe instead of a console
switches to block buffering, so output arrives in lumps; only a pseudoconsole would change that.

Output decodes as UTF-8 until the first invalid sequence, then in the legacy encoding for the rest of the command:

- Windows: the OEM code page. cmd and console programs write to a pipe in the console code page, and a new console
  starts in the OEM one. Characters outside it are already `?` when cmd writes them.
- POSIX: the locale's encoding.

Rejected for Windows:

- `chcp 65001` before the command. Under the windowless console, cmd's built-in commands kept writing OEM bytes with
  65001 active; runs of the identical command line disagreed.
- Decoding the code page by name through Qt. Qt's Windows build has no ICU, so names beyond the UTF encodings do not
  resolve. The OEM decoder calls `MultiByteToWideChar` and holds back a double-byte character split between chunks.

The switch is one-way: a UTF-8 tool that follows legacy output within one command line shows garbled.

## Output panes

`CCommandOutputArea` sits below the panels and holds one `CCommandOutputPane` per running command, side by side.

| Aspect | Behaviour | Reason |
|---|---|---|
| Concurrency | Commands run in parallel, each in its own pane | |
| Claim | A command gets a pane at its first output, or once it outlives a short delay | `copy`, `ren` and `mkdir` finish silently and never touch the UI |
| Reuse | A new command takes over a pane exactly when that pane may close on its own | One rule: output kept for any reason is never recycled |
| Auto-close | A finished pane counts down and closes | |
| Kept open | A failure, a pin, a selection or a scroll cancels the countdown; hovering pauses it | The output is being read, or needs reading |
| Finish line | The output ends with how the command ended: exit code, termination, crash or start failure, and its run time; green for exit code 0, yellow for termination, red otherwise. Exit codes with the high bit set (NTSTATUS) print in hex | The header status is too narrow for the details, and does not survive copying |
| Stop button | Shown while the command runs; ends its process tree. A stopped pane closes and is reused like a successful one | |
| Close button | Enabled once the command has finished | A running command's pane is never closed or reused, so its output always has a pane |
| Focus | A pane never takes focus when shown; it accepts focus on click, for selection and copy | Commands keep coming from the command line |
| View | Follows the tail unless scrolled up; no line wrapping; line count capped; carriage returns dropped | Console layout survives; a chatty command cannot grow the app without limit |

## Process trees and app exit

`QProcess::kill` reaches only `cmd.exe` or `sh`, never what the command launched, so each command owns its whole tree:

- Windows: a job object per command, joined at process creation through `PROC_THREAD_ATTRIBUTE_JOB_LIST` in
  `QProcess::setCreateProcessArgumentsModifier`. A job assigned after start misses whatever the shell launches first.
- POSIX: `setpgid` in `QProcess::setChildProcessModifier`, and `SIGTERM` to the group: processes get to clean up. A
  process that ignores `SIGTERM` survives exit; a second click on Stop sends `SIGKILL`. A process that starts its own
  session escapes both.
- The job has no `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`: a program the command started with `start` outlives the pane
  and the app, like a program started with `&` on POSIX. The cost: a crash of the app leaves the commands running.
- Terminating a command whose shell has exited does nothing: its job may still hold programs started with `start`.

Exit is blocked while any command runs:

- `CMainWindow::closeEvent` prompts before anything else closes, listing the running commands. **Terminate** ends
  their trees and exits; **Cancel** keeps the app open. A running file operation negotiates exit the same way in
  `CFileOperationDialog::closeEvent`.
- Logoff and shutdown do not prompt: Qt 6 emits `commitDataRequest` without closing windows, and the session ends
  the processes.
- A quit that bypasses `closeEvent`, such as `--test-launch`, reaches `CShellCommand`'s destructor, which terminates
  the tree.
- Whether macOS Cmd+Q and Dock Quit reach `closeEvent` is untested.

## Not implemented

- A graceful Ctrl+C. Windows needs an `AttachConsole` and `GenerateConsoleCtrlEvent` sequence (a process attaches to
  one console at a time, which fights parallel panes), a helper process, or a pseudoconsole; POSIX needs `SIGINT` to
  the group.
- A pseudoconsole (ConPTY): line buffering, colour, interactive prompts and Ctrl+C, at the cost of a terminal emulator.
- Dismissing the exit prompt when the last command finishes while it is open; an "Exit when finished" option.
