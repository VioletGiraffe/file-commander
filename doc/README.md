# File Commander — architecture documentation

Internal architecture notes. Dense, fact-first. Verify against code before relying on any `file:line`
citation — paths are stable, line numbers drift.

## What it is

Cross-platform, Total Commander-like orthodox (dual-panel) file manager. Targets Windows (primary),
macOS, Linux, FreeBSD. Goal: consistent UX across desktop OSes. Plugin support for viewers/tools/archives.

- Repo: https://github.com/VioletGiraffe/file-commander (author/git user: Violet Giraffe).
- Language: **C++ (built as C++23 — `c++2b`/`/std:c++latest`)**; README states C++20 as the *minimum*. UI: **Qt 6.8+** (CI pins 6.9.*).
- Build: **qmake** (`.pro`/`.pri`), not CMake. Own code ~16.5k LOC (excl. submodules & vendored 3rdparty).
- Windows: x64 only, Vista+, MSVC 2022 (v143). License: repo-root `LICENSE`.

## Three layers (qmake sub-projects under top-level `file-commander.pro`, `TEMPLATE=subdirs`)

```
+-----------------------------------------------------------------------+
|  qt-app/            FileCommander executable (Qt Widgets GUI)          |
|    CMainWindow, CPanelWidget x2, file-list model/view, dialogs         |
+-----------------------------------------------------------------------+
            |  uses (owns a CController)            ^  listener/observer callbacks
            v                                       |
+-----------------------------------------------------------------------+
|  file-commander-core/   "core" static lib (UI-agnostic; Qt core+gui)   |
|    CController (singleton) -> CPanel per tab -> CFileSystemObject       |
|    file operations, watchers, volumes, search, shell, icons, plugins   |
+-----------------------------------------------------------------------+
            ^  loads .dll/.so/.dylib at runtime, talks via CPluginProxy
            |
+-----------------------------------------------------------------------+
|  plugins/    dynamic libs: imageviewer, textviewer, filecomparison     |
+-----------------------------------------------------------------------+
```

Dependency direction: `qt-app` -> `core` -> (plugin interface). Plugins depend on `core` for the interface
but are loaded dynamically; the core never links them. The UI never talks to `CPanel`/`CFileSystemObject`
without going through `CController` getters.

## Repo layout (top level)

| Path | What |
|------|------|
| `file-commander-core/` | core static lib (`src/`, public `include/`) + `core-tests/` |
| `qt-app/` | GUI executable (`src/`) + `gui-tests/` |
| `plugins/viewer/imageviewer`, `plugins/viewer/textviewer`, `plugins/tools/filecomparisonplugin` | shipped plugins |
| `qtutils/ cpputils/ cpp-template-utils/ thin_io/ text-encoding-detector/ image-processing/ github-releases-autoupdater/` | git submodules (author's libs) — see [build-ci-deps](build-ci-deps.md) |
| `installer/{windows,mac,linux}/` | packaging scripts |
| `extras/win/natvis/file_commander.natvis` | MSVC debugger visualizers |
| `bin/`, `build/` | qmake output (`bin/release/x64/`) |
| `WCX/`, `!TestImages/`, `Debug/`, `Dbgview.exe`, `depends.exe` | local/untracked scratch — not part of the build |

## Doc index (suggested reading order)

1. [core-engine.md](core-engine.md) — the core library: controller, panels, file objects, operations, watchers, volumes, search, shell, icons.
2. [qt-ui.md](qt-ui.md) — the Qt app: main window, panel widget, file-list model/view/delegate, dialogs, settings pages, entry point.
3. [plugins.md](plugins.md) — plugin interface, engine, proxy, WCX archive host, the three shipped plugins.
4. [tabs.md](tabs.md) — the per-panel tabs feature, spanning core + UI + persistence.
5. [threading.md](threading.md) — concurrency model: worker pools, execution queues, the file-op thread, watchers.
6. [persistence.md](persistence.md) — settings keys and the save/restore state machine.
7. [build-ci-deps.md](build-ci-deps.md) — qmake project graph, CI matrix, submodules, vendored 3rdparty.
8. [oddities.md](oddities.md) — flagged bugs, smells, and open questions found while documenting.
9. [release-metadata-audit.md](release-metadata-audit.md) — app/vendor identity, version info, installer, license metadata: current values, defects, and what must NOT be changed.
10. [code-review-plan.md](code-review-plan.md) — how to segment a future review/audit (risk-tiered, with file sets); process doc, not architecture.
11. [TODO.md](TODO.md) — deferred work items with the context needed to act on them; process doc, not architecture.

## Naming & cross-cutting conventions

- Classes prefixed `C` (`CController`, `CPanel`, ...). Plugin enums prefixed differently (`Pa`/`hr`/`ur`/`na`).
- **Items are identified by `qulonglong` hash, not by path.** The hash is a deterministic function of the
  item path (survives restarts). Panel file lists, model rows, selections, cursor memory, and the plugin
  API all key on this hash. `IdentityHash` is used for the hash->object map (the key is already a hash).
- Core->UI notification is via **listener/observer interfaces**, not Qt signals (so the core stays
  widget-agnostic and a notification can be consumed/stop-propagating). Key interfaces:
  `PanelContentsChangedListener`, `CursorPositionListener`, `CurrentPathChangedListener`,
  `CController::IVolumeListObserver`, `CFileOperationListener`, `FileListViewEventObserver`.
- Worker-thread results are marshaled back to the UI thread via `CExecutionQueue` drained on a UI timer.
- `DISABLE_COMPILER_WARNINGS` / `RESTORE_COMPILER_WARNINGS` wrap Qt/3rdparty includes (from cpputils).
- Per-OS code is selected at compile time via `#ifdef _WIN32` etc. and `win*{}`/`mac*{}`/`linux*{}` qmake scopes.

## Glossary

- **Panel / side** — `Panel::LeftPanel` / `RightPanel` (`UnknownPanel` = sentinel). One physical column of the UI.
- **Tab** — an independent `CPanel` (current dir + history + file list). Each side has >=1 tab; `panel(side)`
  returns the *active* tab's `CPanel`. See [tabs.md](tabs.md).
- **Triplet** — UI-side per-tab `{CFileListModel, CFileListSortFilterProxyModel, QItemSelectionModel}` (`PanelTab`).
- **FSO** — `CFileSystemObject`, the wrapper around `QFileInfo` for one file/dir/bundle.
