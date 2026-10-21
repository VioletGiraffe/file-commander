# Release and identity metadata

## Current identity

The application is `File Commander`; current Windows binaries are `FileCommander.exe`; the license is Apache-2.0.
The vendor is `VioletGiraffe` in executable metadata and the installer; settings keep the historical `GitHubSoft`.

Do not change the Qt organization name without migrating settings. Do not change the existing Inno Setup
`AppId=FileCommander`; either change would orphan existing user state or installed-product identity.

## Version ownership

`VERSION_STRING` in `qt-app/src/version.h` is the single source of the version:
- The application and updater use it directly.
- `qt-app.pro` extracts it during qmake; qmake generates the Windows version resource from it. Editing the header requires rerunning qmake locally; clean CI builds already do so.
- The Windows installer reads it from the built executable as `AppVersion`.

## Installer requirements

- Restrict the Windows installer to x64-compatible systems and install in 64-bit mode.
- Use the current automatic Program Files constant.
- Ship the Apache-2.0 `LICENSE` and any future `NOTICE` file with binary distributions.

The installer project is authoritative for packaging details; `main.cpp`, `version.h`, and `qt-app.pro` are the
anchors for settings identity and version propagation.
