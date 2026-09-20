# Tests

The project files are authoritative for the current test set; this is the map. Every suite uses Catch2.

## Layout

`file-commander-core/core-tests/core-tests.pro` builds every suite, including the two that live next to the code they
test:

- `qt-app/gui-tests/fileoperations/`: the file-operation UI.
- `plugins/viewer/csvviewer/tests/`: the CSV viewer.

`core-tests/test-utils/` holds the shared helpers: temporary folder generation, random data, link creation, Qt/Catch2
glue.

## Running them

`scripts/run_tests.bat` and `scripts/run_tests.sh` build `core-tests` and run every executable, then report which
suites failed. The Windows logic lives in `run_tests.ps1`, which the batch file starts; it takes named parameters,
while the shell script takes the same options as positional keywords, in the order listed here:

| | Windows | macOS, Linux |
|---|---|---|
| Debug configuration | `-Configuration debug` | `debug` |
| Build without running, run without building | `-BuildOnly`, `-NoBuild` | `build`, `nobuild` |
| Include the slow cases | `-All` | `all` |
| One suite, plus arguments for it | `-Suite panel "[panel][history]"` | `panel "[panel][history]"` |

Every suite and its default filter live in one table at the top of each script. Each suite runs with Catch2's
`--warn NoTests`, so a filter that matches nothing fails instead of reporting success.

CI drives the same scripts: it builds with `-BuildOnly`/`build`, provisions the RAM disks and second volumes the
environment-gated tests need, then runs with `-NoBuild`/`nobuild`. Its seeded repeat runs select one suite, so
each platform's executable path stays in the script.

Without `all`, the tests that work on generated data are excluded: `[executor]` and `[deleteexecutor]` in
`fileoperations_test`, which build trees of thousands of files, and `[CFileComparator]` in `filecomparator_test`,
which writes about a thousand files of up to 3 MB each. Every other case in both suites still runs. The cost is
mostly in the comparator tests; the file-operation ones are quick unless a second volume is provisioned for the
cross-volume cases.

The scripts take the Qt kit from `QT_ROOT_DIR`, or from a git-ignored `local-env.bat`/`local-env.sh` beside them;
the shell script also falls back to a `qmake` already on PATH.

## Kinds

| Kind | Where |
|------|-------|
| Unit tests of pure logic | parsers, path handling, placeholder expansion, name filters |
| Integration tests on the real filesystem, in generated temporary trees | file operations, comparison, panels, search |
| Randomized runs, reproducible by `--std-seed <seed>` | `fileoperations_test`, `filecomparator_test` |
| Fault injection through `operationtesthooks`, compiled out of production builds | file operations |
| Environment-gated coverage: skipped unless the variable is set | cross-volume, case-sensitive volume, symlinks (`FILE_COMMANDER_TEST_*`) |
| Headless widget tests, runnable under `QT_QPA_PLATFORM=offscreen` | file-operation dialogs and prompts |
| Launch smoke test: the release binary with `--test-launch` | CI build job, every platform |

## Coverage by component

| Executable | Component |
|------------|-----------|
| `fileoperations_test` | File-operation engine: copy, move, delete, staged copy, destination and name resolution, cross-volume, hostile names |
| `filecomparator_test` | File and folder comparison |
| `fso_test` | `CFileSystemObject`, and the `QDir`/`QFileInfo` behaviors the core relies on |
| `fso_test_high_level` | `CFileSystemObject` path semantics: hierarchy, trailing separators, normalization |
| `panel_test` | `CPanel`: navigation, history, current item, content access, refresh notifications, lifetime |
| `filesearchengine_test` | Search engine: name filters, content search, engine behavior |
| `filesystemhelpers_test` | Path quoting and shell word splitting |
| `userprograms_test` | Programs-menu placeholder expansion |
| `fileoperations_gui_test` | File-operation UI from `qt-app/src`: dialogs, prompts, launch routing |
| `csvviewer_test` | CSV viewer: parser, table model, comment list model |

No suite covers process launching, volume enumeration, favorites, settings, the UI outside file operations, or any
plugin other than the CSV viewer. The `cpputils`, `cpp-template-utils`, and `thin_io` submodules have their own
suites, which this project neither builds nor runs.

## CI

CI runs the test job only when a changed file matches the `changes` job's path filter in `CI.yml`. A new test, or a
source it compiles, outside the filtered paths needs its path added there, or changing it never triggers the tests.
Each platform's steps in `CI.yml` name every test executable (on Linux, the deployment step too), so a new suite
must be added to each of them.
