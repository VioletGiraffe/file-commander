# Build, CI, and dependencies

## Build system: qmake

Top-level `file-commander.pro` (`TEMPLATE = subdirs`). Sub-projects + their `depends` (build order).

Everything that links `core` also links `thin_io`: core's filesystem helpers and its whole file-operation module
call into it.

- `global.pri`: `CONFIG += strict_c++ c++2b` (**C++23**) and other shared/global build flags.
- **Requirements:** C++20-capable compiler (README floor) — actually built as C++23; Qt 6.8+ (CI uses 6.9.*).
  Windows x64 only, MSVC 2022 / v143.

### Build artifacts

Output binaries: `bin/release/x64/`. Root `Makefile` is qmake-generated. Installers in
`installer/{windows,mac,linux}/`.

## CI

Github CI configured, see `.github/workflows/CI.yml`

## Tests

`file-commander-core/core-tests/core-tests.pro` builds the automated test suite, including the file-operation
GUI tests under `qt-app/gui-tests/fileoperations/`. `qt-app/gui-tests/combobox/` is a manual harness. See
`file-commander-core/core-tests/core-tests.pro` and `qt-app/gui-tests/gui-tests.pro` for the current list of test
projects; each test project's `.pro` file is the source of truth for its test sources.

`fileoperations_test` and `filecomparator_test` accept `--std-seed <seed>`. Genuine cross-volume tests additionally
require `FILE_COMMANDER_TEST_SECOND_VOLUME` to name a writable directory on a filesystem different from the one
containing `TEMP`; without it, that coverage is skipped. Headless GUI-test runs may use
`QT_QPA_PLATFORM=offscreen`.

## Dependencies

### Submodules (`.gitmodules`, all github.com/VioletGiraffe — the author's own libs)

| Submodule | Role |
|-----------|------|
| **qtutils** | Reusable Qt classes: `CSettings`, threading (`CWorkerThreadPool`, `CExecutionQueue`, `CPeriodicExecutionThread`, `CInterruptableThread`), `CHistoryList`, natural sort (`CNaturalSorterQCollator`), `CSettingsDialog`, string helpers (`QSL`). |
| **cpputils** | Plain-C++ utils: `assert_r`/`AdvancedAssert`, `CTimeElapsed`, named-type wrappers, compiler-warning macros, threading. |
| **cpp-template-utils** | Header-only template/metaprogramming + container algorithms + preprocessor helpers. |
| **thin_io** | Cross-platform low-level file I/O on native OS APIs (no `<fstream>`/`<stdio>`); `thin_io::file` + metadata used by the file-operation engine (`CStagedFileCopy`, `CFileSystemMutator`). |
| **text-encoding-detector** | Detects text encoding of bytes -> QString. Backs the text-viewer plugin. |
| **image-processing** | Image processing lib. Backs the image-viewer plugin.
| **github-releases-autoupdater** | Update check + download for GitHub-release-distributed builds (Windows-installer focused). |

### Vendored 3rdparty (not submodules)

- `3rdparty/ankerl/unordered_dense.h` — fast hash map (`segmented_map` for panel file lists).
- `3rdparty/magic_enum` — enum reflection (used in `CFileStatsWindow`).
- `plugins/viewer/textviewer/3rdparty/diegoiast/qutepart-cpp` — Kate-style syntax highlighting for the text viewer.
