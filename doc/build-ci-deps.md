# Build, CI, and dependencies

## Build system: qmake

`file-commander.pro` is the authoritative project graph; each project `.pro` and included `.pri` file lists its
sources and platform branches. `global.pri` holds all shared compiler configuration; project files add only
project-specific flags. The supported floor is Qt 6.8+ with a C++23-capable compiler. Windows builds are x64 with
MSVC 2022/v143.

Everything that links `core` also links `thin_io`, because core filesystem helpers and file operations call it.

Every project outputs to `bin/{debug,release}`: the executable, the static libraries, and the plugin libraries. A
dev build therefore already has the plugins beside the executable, where the plugin loader looks. `build/` holds
intermediates.

## CI

`.github/workflows/CI.yml` is the source of truth for the current matrix, tool versions, packaging, smoke test,
test invocation, and release workflow.

## Tests

`file-commander-core/core-tests/core-tests.pro` builds the automated suite. The file-operation GUI tests are part of
it but live with the UI sources in `qt-app/gui-tests/fileoperations/`. `core-tests/test-utils/` holds the shared
helpers: temporary folder generation, random data, link creation, Qt/Catch2 glue. The project files are authoritative
for the current test set.

`fileoperations_test` and `filecomparator_test` accept `--std-seed <seed>`. Cross-volume and case-sensitive-volume
coverage use `FILE_COMMANDER_TEST_SECOND_VOLUME` and `FILE_COMMANDER_TEST_CASE_SENSITIVE_VOLUME`; see the tests and
CI workflow for their contracts. Headless GUI tests may use `QT_QPA_PLATFORM=offscreen`.

## Dependencies

### Submodules

`.gitmodules` is authoritative for the current set and repository URLs.

| Submodule | Role |
|-----------|------|
| **qtutils** | Qt settings, widgets/dialog helpers, history, natural sorting, and string helpers, plus `CImageViewerWidget`, the pan/zoom image view behind the image-viewer plugin. |
| **cpputils** | Assertions, threading/execution queues, compiler helpers, and general C++ utilities. |
| **cpp-template-utils** | Header-only template/metaprogramming + container algorithms + preprocessor helpers. |
| **thin_io** | Cross-platform native file I/O and metadata used by core filesystem and operation code. |
| **text-encoding-detector** | Detects text encoding of bytes -> QString. Backs the text-viewer plugin. |
| **image-processing** | Image processing library used by the image-viewer plugin. Qt-free itself; Qt consumers include its header-only `resize/qimage_resize.h` bridge. |
| **github-releases-autoupdater** | Update check + download for GitHub-release-distributed builds (Windows-installer focused). |
