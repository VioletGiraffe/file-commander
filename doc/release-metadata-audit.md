# Release and identity metadata

## Current identity

The application is `File Commander`; current Windows binaries are `FileCommander.exe`; the license is Apache-2.0.
Historical vendor identifiers differ across settings (`GitHubSoft`), executable metadata (`GithubSoft`), and public
installer/copyright fields (`VioletGiraffe`).

Do not change the Qt organization name without migrating settings. Do not change the existing Inno Setup
`AppId=FileCommander`; either change would orphan existing user state or installed-product identity.

## Version ownership

`VERSION_STRING` in `qt-app/src/version.h` is the live version consumed by the application and updater.
`qt-app.pro` extracts it during qmake so Windows executable metadata can share the value. Editing the header requires
rerunning qmake locally; clean CI builds already do so.

The remaining single-sourcing work is to:

1. Let qmake generate Windows version resources while retaining the application icon through `RC_ICONS`.
2. Set consistent `QMAKE_TARGET_*` identity fields.
3. Have the Windows installer read the built executable version and expose it as `AppVersion`.

## Installer requirements

- Restrict the Windows installer to x64-compatible systems and install in 64-bit mode.
- Use the current automatic Program Files constant.
- Ship the Apache-2.0 `LICENSE` and any future `NOTICE` file with binary distributions.

The installer project is authoritative for packaging details; `main.cpp`, `version.h`, and `qt-app.pro` are the
anchors for settings identity and version propagation.
