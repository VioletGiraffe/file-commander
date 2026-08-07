# File Commander

File Commander is a cross-platform orthodox dual-panel file manager with plugin support. Windows is the primary
target; macOS and Linux are supported, while FreeBSD is best-effort where the Linux implementation works unchanged.

[![CI](https://github.com/VioletGiraffe/file-commander/actions/workflows/CI.yml/badge.svg)](https://github.com/VioletGiraffe/file-commander/actions/workflows/CI.yml)

![Windows screenshot](https://github.com/VioletGiraffe/file-commander/blob/gh-pages/screenshots/Windows/screenshot.png?raw=true)

## Download for Windows

[Get the latest release](https://github.com/VioletGiraffe/file-commander/releases/latest). Current Windows builds
are x64.

## Issues

See [known bugs](https://github.com/VioletGiraffe/file-commander/labels/bug) or
[report an issue](https://github.com/VioletGiraffe/file-commander/issues/new).

## Building

The repository uses submodules. After cloning, run `update_repository.bat` on Windows or `update_repository.sh` on
Linux/macOS. The same script updates the main repository and its submodules later.

Requirements: a C++23-capable compiler and Qt 6.8 or newer.

- Windows: Visual Studio 2022 or later. Run `qmake -tp vc -r` to generate the solution.
- Linux: run Qt 6's `qmake -r`, then `make`.
- macOS: open the project in Qt Creator, or run `qmake -r -spec macx-xcode` for an Xcode project.

The [CI workflow](.github/workflows/CI.yml) is the reference for supported build and test commands.
