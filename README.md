# FILE COMMANDER

   Cross-platform Total Commander-like orthodox (dual-panel) file manager for Windows, Mac,  Linux and FreeBSD with support for plugins. The goal of the project is to provide consistent user experience across all the major desktop systems. 

[![CI](https://github.com/VioletGiraffe/file-commander/actions/workflows/CI.yml/badge.svg)](https://github.com/VioletGiraffe/file-commander/actions/workflows/CI.yml)

[![CodeFactor](https://www.codefactor.io/repository/github/violetgiraffe/file-commander/badge/master)](https://www.codefactor.io/repository/github/violetgiraffe/file-commander/overview/master)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/190add40753b46edbaa1327068263263)](https://www.codacy.com/gh/VioletGiraffe/file-commander/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=VioletGiraffe/file-commander&amp;utm_campaign=Badge_Grade)

![Windows screenshot](/../gh-pages/screenshots/Windows/screenshot.png?raw=true)

### Download for Windows

*<a href="https://github.com/VioletGiraffe/file-commander/releases/latest">Get the latest release</a>*    
Windows Vista and later systems are supported, x64 only (but older releases supported x86). Windows XP is not supported.

### Known Issues
For the list of known issues, refer to the project issues on Github, sort by the "bug" label. Or just use <a href="https://github.com/VioletGiraffe/file-commander/labels/bug">this link</a>.

### Reporting an issue
<a href="https://github.com/VioletGiraffe/file-commander/issues/new">Create an issue</a> on the project's page on Github.

### Contributing

***Cloning the repository***

   The main git repository has submodules, so you need to execute the `update_repository` script (available as .bat for Windows and .sh for Linux / Mac) after cloning file-commander to clone the nested repositories. Subsequently, you can use the same `update_repository` script at any time to pull incoming changes to the main repo, as well as to all the subrepos, thus updating everything to the latest revision.

***Building***

* A compiler with C++20 support is required.
* Build with Qt 6.8 or newer.
* Windows: you can build using either Qt Creator or Visual Studio for IDE. Visual Studio 2022 or later is required (v143 toolset or newer). Run `qmake -tp vc -r` to generate the solution for Visual Studio. I have not tried building with MinGW, but it should work as long as you enable C++20 support.
* Linux: `cd` to directory with project, run `qmake -r` to generate Makefile and build via `make -j`. Make sure it's qmake from Qt 6 installation and not Qt5 (usually `qmake6 -r` works to ensure that).
* Mac OS X: You can use either Qt Creator (simply open the project in it) or Xcode (run `qmake -r -spec macx-xcode` and open the Xcode project that has been generated). Or you can build from command line with `qmake -r` followed by `make -j`.

See the Github workflow .yml file for reference on building the project.
