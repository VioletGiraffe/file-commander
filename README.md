File Commander
==============

   Qt-based cross-platform Total Commander-like orthodox (dual-panel) file manager for Windows, Mac and Linux with support for plugins. The goal of the project is to provide consistent user experience across all the major desktop systems.

![Windows screenshot](/../gh-pages/screenshots/Windows/Clip.jpg?raw=true)

###Download for Windows

*<a href="https://github.com/VioletGiraffe/file-commander/releases/latest">Get the latest release</a>*    
Windows Vista and later systems are supported (x32 and x64). Windows XP is not supported.

###Known Issues
For the list of known issues, refer to the project issues on Github, sort by the "bug" label. Or just use <a href="https://github.com/VioletGiraffe/file-commander/labels/bug">this link</a>.

###Reporting an issue
<a href="https://github.com/VioletGiraffe/file-commander/issues/new">Create an issue</a> on the project's page on Github.

###Contributing

***Cloning the repository***

   The main git repository has submodules, so you need to execute the `update_repository` script (available as .bat for Windows and .sh for Win / Mac) after cloning file-commander to clone the nested repositories. Subsequently, you can use the same `update_repository` script at any time to pull incoming changes to the main repo, as well as to all the subrepos, thus updating everything to the latest revision.

***Building***

* A compiler with C++ 0x/11 support is required (std::thread, lambda functions etc.).
* Qt 5.4 or newer required.
* Windows: you can build using either Qt Creator or Visual Studio for IDE. Visual Studio 2013 or newer is required - v120 toolset or newer. Run `qmake -tp vc -r` to generate the solution for Visual Studio. I have not tried building with MinGW, but it should work as long as you enable C++ 11 support.
* Linux: open the project file in Qt Creator and build it.
* Mac OS X: You can use either Qt Creator (simply open the project in it) or Xcode (run `qmake -r -spec macx-xcode` and open the Xcode project that has been generated).
