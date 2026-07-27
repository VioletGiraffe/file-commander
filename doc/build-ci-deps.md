# Build, CI, and dependencies

## Build system: qmake

`file-commander.pro` is the authoritative project graph; each project `.pro` and included `.pri` file lists its
sources and platform branches. `global.pri` selects C++23 and shared optimization flags. The supported floor is
Qt 6.8+ and a C++20-capable compiler, although the project itself is built as C++23. Windows builds are x64 with
MSVC 2022/v143.

Everything that links `core` also links `thin_io`, because core filesystem helpers and file operations call it.

### Build artifacts

Output binaries go to `bin/{release,debug}/{x64,x86}/` as selected in the project files; supported Windows builds
are x64. Installer entry points live under `installer/{windows,mac,linux}/`.

## CI

`.github/workflows/CI.yml` is the source of truth for the current matrix, tool versions, packaging, smoke test,
test invocation, and release workflow.

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

### Submodules

`.gitmodules` is authoritative for the current set and repository URLs.

| Submodule | Role |
|-----------|------|
| **qtutils** | Qt settings, widgets/dialog helpers, history, natural sorting, and string helpers. |
| **cpputils** | Assertions, threading/execution queues, compiler helpers, and general C++ utilities. |
| **cpp-template-utils** | Header-only template/metaprogramming + container algorithms + preprocessor helpers. |
| **thin_io** | Cross-platform native file I/O and metadata used by core filesystem and operation code. |
| **text-encoding-detector** | Detects text encoding of bytes -> QString. Backs the text-viewer plugin. |
| **image-processing** | Image processing library used by the image-viewer plugin. |
| **github-releases-autoupdater** | Update check + download for GitHub-release-distributed builds (Windows-installer focused). |

### Vendored 3rdparty (not submodules)

- `3rdparty/ankerl/unordered_dense.h` — fast hash map (`segmented_map` for panel file lists).
- `3rdparty/magic_enum` — enum reflection (used in `CFileStatsWindow`).
- `plugins/viewer/textviewer/3rdparty/diegoiast/qutepart-cpp` — Kate-style syntax highlighting for the text viewer.
