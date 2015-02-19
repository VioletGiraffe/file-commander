File Commander
==============

   Qt-based cross-platform Total Commander-like dual panel file manager for Windows, Mac and Linux with support for plugins.

![Windows screenshot](/../gh-pages/screenshots/Windows/Clip.jpg?raw=true)

***Cloning the repository***

   The main git repository has submodules, so you need to execute the `update_repository` script (available as .bat for Windows and .sh for Win / Mac) after cloning file-commander to clone the nested repositories. Subsequently, you can use the same `update_repository` script at any time to pull incoming changes to the main repo, as well as to all the subrepos, thus updating everything to the latest revision.

***Building***

* A compiler with C++ 0x/11 support is required (std::thread, lambda functions)
* Qt 5 required
* Mac and Linux: open the project file in Qt Creator and build it.
* Windows: you can build using Qt Creator or Visual Studio for IDE. Visual Studio 2012 or 2013 is required - v110 or v120 toolset (run `qmake -tp vc -r` to generate the solution for Visual Studio). I have not tried building it with MinGW, but it should work as long as you enable C++ 11 support.
* Qt 5 only.
