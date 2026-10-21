# Tests

The project files are authoritative for the current test set; this is the map. Every suite uses Catch2.

## Layout

`file-commander-core/core-tests/core-tests.pro` builds every suite, including the ones that live next to the code they
test:

- `file-operations/tests/`: the file-operation engine, its dialogs, and folder comparison.
- `qt-app/gui-tests/fileoperations/`: launching file operations from the UI.
- `qt-app/gui-tests/filelist/`: the file-list model.
- `plugins/viewer/csvviewer/tests/`: the CSV viewer.

`file-operations/test-utils/` holds the shared helpers: temporary folder generation, random data, link creation, Qt/Catch2
glue. Every suite links `catch2_runner` (`cpp-template-utils/3rdparty/catch2/`), Catch2's implementation compiled once
for all of them.

The suites link the core and file-operations libraries, built in the same tree. The GUI suites also compile the
application sources they test, listed in their `.pro`; the application itself is not built. A passing run says nothing
about a source no suite lists, most of `qt-app/src` for one: it is not even compiled.

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

Without `all`, the slow generated-data cases are excluded; `file-operations/doc/testing.md` lists them.

To run on FAT32 or exFAT, point `TEMP` and `TMP` at the volume: every test works in a `QTemporaryDir`.

Tests that need what those filesystems lack skip themselves there: links everywhere, and on POSIX also FIFOs,
sockets and permission bits (`filesystemHasNoLinks()`, `filesystemIsFatFamily()`).

CI runs every suite this way on an exFAT and a FAT32 volume, on each platform.

`filelist_test`'s `[timing]` cases run only when named. They print:

- what an in-place model update costs against a reset, which is where the model's reset threshold comes from;
- what one name filter change costs, with and without a view;
- what the collator's sort costs by string and by sort key, and the memory of the keys and of a model beyond its rows.

The scripts take the Qt kit from `QT_ROOT_DIR`, or from a git-ignored `local-env.ps1`/`local-env.sh` beside them;
the shell script also falls back to a `qmake` already on PATH.

## Kinds

| Kind | Where |
|------|-------|
| Unit tests of pure logic | parsers, path handling, placeholder expansion, name filters |
| Integration tests on the real filesystem, in generated temporary trees | file operations, comparison, panels, search |
| Randomized runs, reproducible by `--std-seed <seed>` | `fileoperations_test`, `foldercomparison_test`, `filelist_test` |
| Fault injection through `operationtesthooks`, compiled out of production builds | file operations |
| Coverage needing a filesystem capability: skipped where absent, required where its `FILE_OPERATIONS_TEST_*` variable is set | cross-volume (the variable also names the volume), case-sensitive volume, symlinks |
| Headless widget tests, runnable under `QT_QPA_PLATFORM=offscreen` | file-operation dialogs and prompts, the file list view |
| Launch smoke test: the release binary with `--test-launch=N`, failing unless the N plugins the build produced all load | CI build job, every platform |

## Coverage by component

| Executable | Component |
|------------|-----------|
| `fileoperations_test` | File-operation engine: copy, move, delete, staged copy, destination and name resolution, cross-volume, hostile names |
| `foldercomparison_test` | File content and folder comparison |
| `fileoperationsui_test` | File-operation dialog and decision prompt |
| `fso_test` | `CFileSystemObject`'s rules on entries built in memory: name split, type, hidden, executable, links, `[..]` |
| `fso_test_high_level` | `CFileSystemObject` on the real filesystem: path semantics, name split, hidden and executable entries, links; the panel listing and its `[..]` |
| `panel_test` | `CPanel`: navigation, history, current item, content access, refresh notifications, lifetime |
| `filesearchengine_test` | Search engine: name filters, content search, engine behavior |
| `filesystemhelpers_test` | Path quoting, shell word splitting, device lookup for a path |
| `userprograms_test` | Programs-menu placeholder expansion |
| `shell_test` | `CShellCommand` on real commands: output, exit code, working folder, escape sequences, termination. Which command lines launch without the shell (Windows); the detached launch's command lookup (Linux, macOS) |
| `fileoperations_gui_test` | File-operation launch from `qt-app/src`: destination intent, routing to the dialog or the native shell |
| `filelist_test` | File list from `qt-app/src`, on rows built in memory: the model's sorting, filtering, lookups and in-place updates under Qt's model contract; the view's rename editor and scroll position through updates |
| `csvviewer_test` | CSV viewer: parser, table model, comment list model |

No suite covers a launch that opens a window (programs, terminals), volume enumeration, favorites, settings, the UI outside file operations and the
file list, or any plugin other than the CSV viewer. The `cpputils`, `cpp-template-utils`, `thin_io`,
`image-processing` and `text-encoding-detector` submodules have their own suites, which this project neither builds
nor runs. After a `thin_io` change, run `thin_io/scripts/run_tests.bat` (or `.sh`) as well.

## Listing benchmark

`listing_benchmark` times listing one folder four ways: `QDir` with the panel's filters, `thin_io::list_directory`
with basic and with full detail, and `listDirectoryForPanel`, the panel's own listing. `core-tests.pro` builds it with
LTO, as the application is built; the test scripts never run it. `--help` lists the options. The results: [listing-performance.md](listing-performance.md).

- `--generate <root>` creates flat folders of 1k, 10k and 100k entries once, to be reused. The files are empty: content
  does not affect a listing.
- `scripts/listing_benchmark.ps1` owns the VHDX volumes the timings run on, and needs an elevated prompt; start it
  through `powershell -ExecutionPolicy Bypass -File`. `-Setup` creates a fixed 512 MB VHDX per disk and generates the
  folders on it. `-Warm` and `-Samples N` mount it read-only and time every folder on it, passing trailing arguments on
  to the executable.
- Warm: the variants alternate after untimed rounds; the panel listing's memory per entry is reported too, where the
  allocator exposes it. Any folder can be timed by passing it to the executable directly.
- Cold: the executable's `--cold` runs a remount command before every single listing, which discards the volume's
  cache. Each script supplies its own (`-Remount`, `remount`).
- `scripts/listing_benchmark.sh` is the Linux counterpart, taking positional keywords: `generate` and `warm` need no
  privileges, `cold <image> <samples>` creates an ext4 image and mounts it afresh for every listing, as root. Its loop
  device uses direct I/O, since unmounting does not drop the page cache holding the image file itself.
- Confirm cold numbers are cold before trusting them: the first listing after a remount must read from the disk
  (Resource Monitor's Disk tab, `iostat`), and a folder must list slower from an HDD than from an SSD.
- The drive's own cache survives a remount, so some cold samples are partly served from it. The executable shuffles
  the order every round, so these hits land on random variants.
- Entry counts differ by design: `thinio` and `thiniofull` list no `[..]`. A `panel` count below `qt` means entries
  the panel dropped.

## File list map benchmark

`filelist_hashmap_benchmark` times candidate containers for `FileListHashMap` on the work one listing does to it; the
source's header lists the phases. Entries are built in memory, so no folders are needed. It is built like
`listing_benchmark` and never run by the test scripts; `--help` lists the options. A summary follows: every
candidate's total at every size, against `FileListHashMap`, with and without `reserve`. The results:
[filelist-map-performance.md](filelist-map-performance.md).

- Every sample covers at least 256k entries, batching small maps. Candidates run in a new shuffled order every round.
- Heap figures count the container's own allocations only: the path strings are shared by every candidate.
- One thread runs everything, whereas the application copies the values on the UI thread after a worker built the map.
  Small maps are therefore warmer in cache here than in the application.
- The default sizes up to 1M entries need about 0.5 GB of memory.

## CI

CI runs the test job only when a changed file matches the `changes` job's path filter in `CI.yml`. A new test, or a
source it compiles, outside the filtered paths needs its path added there, or changing it never triggers the tests.
