# Build, CI, and dependencies

## Build system: qmake

Top-level `file-commander.pro` (`TEMPLATE = subdirs`). Sub-projects + their `depends` (build order):

```
qt_app  depends: file_commander_core qtutils imageviewerplugin textviewerplugin autoupdater image-processing filecomparisonplugin
file_commander_core   (file-commander-core/)              -> static lib "core"
qtutils, cpputils, cpp-template-utils, thin_io            submodule libs
text_encoding_detector (text-encoding-detector/text-encoding-detector)
autoupdater            (github-releases-autoupdater/)
image-processing
imageviewerplugin   depends: file_commander_core image-processing qtutils
textviewerplugin    depends: file_commander_core text_encoding_detector qtutils
filecomparisonplugin depends: qtutils file_commander_core
```

- `global.pri`: `CONFIG += strict_c++ c++2b` (**C++23**), removes c++17/c++2a. Windows Release adds `/GL`
  (whole-program opt) + `/LTCG:INCREMENTAL`, `/OPT:REF /OPT:ICF`. ccache on mac/linux when present.
- `file-commander-core/config.pri`: `QT = core widgets gui`, `staticlib`, `DEFINES += PLUGIN_MODULE`,
  MSVC `/std:c++latest /permissive- /Zc:__cplusplus /W4`, `WIN32_LEAN_AND_MEAN NOMINMAX`; non-Win non-ARM
  `-msse4.1`; output dirs `bin/{release,debug}/{x64,x86}`, intermediates `build/...`. INCLUDEPATH pulls in
  `../qtutils ../cpputils ../cpp-template-utils ../thin_io/src ../3rdparty`.
- **Requirements:** C++20-capable compiler (README floor) — actually built as C++23; Qt 6.8+ (CI uses 6.9.*).
  Windows x64 only, MSVC 2022 / v143.

### Local build commands

| OS | Commands |
|----|----------|
| Windows (VS) | `qmake -tp vc -r` -> build `file-commander.sln` (v143, Release) |
| Windows (Qt Creator) | open `file-commander.pro` |
| Linux | `qmake6 -r` (ensure Qt6 qmake) then `make -j` |
| macOS | `qmake -r && make -j`, or `qmake -r -spec macx-xcode` for Xcode |

Output binaries: `bin/release/x64/`. Root `Makefile` is qmake-generated. Installers in
`installer/{windows,mac,linux}/`. **Per project rule: don't build here — the user builds & verifies.**

### Cloning

Repo uses submodules. After clone run `update_repository.bat`/`.sh` (also pulls + updates all subrepos).
`push_repository.*` pushes main + subrepos.

## CI (`.github/workflows/CI.yml`)

Triggers: push, pull_request, workflow_dispatch. Matrix (`fail-fast: false`): `ubuntu-22.04`, `macos-14`,
`windows-latest`. Checks out submodules recursively. Installs Qt **6.9.*** (`qtbase icu qtsvg` + modules
`qt5compat qtimageformats`).

**`build` job per OS:**
- Sys-info + `cloc` line count (Windows).
- **`dorny/paths-filter`** sets `tests_relevant` if `file-commander-core/src/**`,
  `file-commander-core/core-tests/**`, or `.github/workflows/CI.yml` changed. Tests only run when true.
- Build the installer/dmg/AppImage:
  - Windows: `installer/windows/create_installer.bat`, then xcopy MSVC + Qt runtime into `bin/release/x64/`.
  - macOS: `installer/mac/create_dmg.sh`.
  - Linux: `qmake -r CONFIG+=release && make -j`, then `linuxdeployqt` -> AppImage (bundling the 3 plugin .so).
- **Smoke test:** launch `FileCommander --test-launch` (auto-quits after 5 s); Linux under `xvfb-run`.
- **Build + run core tests** (when `tests_relevant`): `fso_test`, `fso_test_high_level`,
  `operationperformer_test` **x20 with random `--std-seed`**, `filecomparator_test` (random seed). Windows
  runs the op-performer tests against a **512 MB RAM disk (R:)** set as TEMP/TMP. See [../doc/qt-ui.md]
  and core-tests below.
- Upload artifacts: `FileCommander.exe` / `.dmg` / `.AppImage`.

**`create-release` job** (only on tag push `refs/tags/*`): downloads the three artifacts, generates a
changelog from `git log` between the previous and current tag, publishes a GitHub Release via
`softprops/action-gh-release`. (Bug: the `body:` line has a stray trailing `}` — see [oddities.md](oddities.md).)

## Tests

Core tests: `file-commander-core/core-tests/` (`core-tests.pro` aggregates sub-`.pro`s). Build mirrors CI:
`qmake -tp vc -r` + msbuild (Win) or `qmake -r CONFIG+=release && make -j` (Unix). Executables -> `bin/release/x64/`.

| Test | Source | Notes |
|------|--------|-------|
| `fso_test` | `filesystemobject/fso_test.cpp` | Uses `QFileInfo_Test`/`QDir_Test` mocks (`CFILESYSTEMOBJECT_TEST`). `qdir_test.*`, `qfileinfo_test.*`. |
| `fso_test_high_level` | `filesystemobject-high-level/fso_test_high_level.cpp` | Real filesystem. |
| `operationperformer_test` | `operationperformer/operationperformertest.cpp` | Stress: 20x random seed; RAM disk on Windows CI. |
| `filecomparator_test` | `filecomparator/filecomparator_test.cpp` | Random seed. |

Shared test helpers in `core-tests/test-utils/src/`: `ctestfoldergenerator`, `crandomdatagenerator`,
`cfolderenumeratorrecursive`, `foldercomparator`, `qt_helpers`.

GUI tests: `qt-app/gui-tests/` (e.g. `combobox/`) — minimal, manual, not in CI.

## Dependencies

### Submodules (`.gitmodules`, all github.com/VioletGiraffe — the author's own libs)

| Submodule | Role |
|-----------|------|
| **qtutils** | Reusable Qt classes: `CSettings`, threading (`CWorkerThreadPool`, `CExecutionQueue`, `CPeriodicExecutionThread`, `CInterruptableThread`), `CHistoryList`, natural sort (`CNaturalSorterQCollator`), `CSettingsDialog`, string helpers (`QSL`). |
| **cpputils** | Plain-C++ utils: `assert_r`/`AdvancedAssert`, `CTimeElapsed`, named-type wrappers, compiler-warning macros, threading. |
| **cpp-template-utils** | Header-only template/metaprogramming + container algorithms + preprocessor helpers. |
| **thin_io** | Cross-platform low-level file I/O on native OS APIs (no `<fstream>`/`<stdio>`); `thin_io::file` used by `CFileManipulator`. |
| **text-encoding-detector** | Detects text encoding of bytes -> QString. Backs the text-viewer plugin. |
| **image-processing** | Image processing lib. Backs the image-viewer plugin. (One of two submodules currently showing local working-tree changes.) |
| **github-releases-autoupdater** | Update check + download for GitHub-release-distributed builds (Windows-installer focused). |

`git submodule status` working-tree state changes over time; at last check `image-processing` and
`text-encoding-detector` had local modifications (`?` in `git status`). The rest were clean on `master`.

### Vendored 3rdparty (not submodules)

- `3rdparty/ankerl/unordered_dense.h` — fast hash map (`segmented_map` for panel file lists).
- `3rdparty/magic_enum` — enum reflection (used in `COperationPerformer` for `HaltReason`).
- `plugins/viewer/textviewer/3rdparty/diegoiast/qutepart-cpp` — Kate-style syntax highlighting for the text viewer.

### Other root items

`extras/win/natvis/file_commander.natvis` (MSVC debug visualizers), `Dbgview.exe`/`depends.exe` (local
tools), `.qtcreator/ .vs/ .qtc_clangd/` (IDE), `WCX/ !TestImages/ Debug/` (untracked scratch).
