# File Commander architecture notes

These documents map the codebase and record cross-file invariants or decisions that are difficult to recover from a
local code read. Source remains authoritative for APIs and current feature lists.

## Project shape

File Commander is a cross-platform orthodox dual-panel file manager for Windows, macOS, and Linux; FreeBSD is
best-effort where the Linux implementation works unchanged. The project uses C++23, Qt 6.8+, and qmake.

```
qt-app/                  Qt Widgets executable
    |
    v
file-commander-core/     controller, panels, filesystem and operations
    ^
    |
plugins/                 dynamically loaded viewers and tools
```

The UI reaches panels and filesystem objects through `CController`. Native plugins compile against the core
plugin interface, but the core discovers and loads them at runtime rather than linking them. See the top-level
`file-commander.pro` for the current project graph and each project's `.pro`/`.pri` files for its sources.

Every project builds into `bin/{debug,release}`; see [build-ci-deps.md](build-ci-deps.md). `installer/` holds the
per-platform packaging and `extras/win/natvis` the debugger visualizers.

## Vocabulary

| Term | Meaning | In code |
|------|---------|---------|
| side | the left or right half of the window | `Panel` enum value |
| tab | one directory view on a side, owning its directory, history, and list | `CPanel` |
| panel | either of the above depending on context; `CController::panel(side)` returns the side's active tab | `Panel`, `CPanel` |
| panel widget | one side's entire UI, hosting the shared file-list view | `CPanelWidget` |
| triplet | one tab's model, sort/filter proxy, and selection model in the panel widget | `CPanelWidget::PanelTab` |

## Source map

| Area | Start here |
|------|------------|
| Core facade and tabs | `file-commander-core/src/ccontroller.{h,cpp}` |
| One tab's directory state | `file-commander-core/src/cpanel.{h,cpp}` |
| Filesystem entry wrapper | `file-commander-core/src/cfilesystemobject.{h,cpp}` |
| Directory listing type (`FileListHashMap`) | `file-commander-core/src/detail/file_list_hashmap.h` |
| Recursive traversal (`scanDirectory`) | `file-commander-core/src/directoryscanner.{h,cpp}` |
| Directory change watchers | `file-commander-core/src/filesystemwatcher/` |
| Copy, move, permanent delete | `file-commander-core/src/fileoperations/` and `fileoperations.pri` |
| File and content search | `file-commander-core/src/filesearchengine/`, `qt-app/src/filessearchdialog/` |
| Main window and command routing | `qt-app/src/cmainwindow.{h,cpp,ui}` |
| Panel UI and file-list MVC | `qt-app/src/panel/` |
| Blocking shell operations | `qt-app/src/cshelloperationrunner.{h,cpp}` |
| Process launching and command output | `file-commander-core/src/shell/`, `qt-app/src/commandoutput/` |
| Programs menu | `file-commander-core/src/userprograms/`, `qt-app/src/programseditor/` |
| Native plugin API and loader | `file-commander-core/src/plugininterface/`, `pluginengine/` |
| Shipped plugins | `plugins/viewer/`, `plugins/tools/` |
| Build graph | `file-commander.pro`; see [build-ci-deps.md](build-ci-deps.md) |
| Tests | `file-commander-core/core-tests/core-tests.pro`; see [testing.md](testing.md) |

## Invariants to carry into code reading

1. Until controller shutdown, each side owns at least one tab; a tab owns a `CPanel`.
   `CController::panel(side)` returns the active tab, while tab IDs remain stable across reordering.
2. Each UI tab owns a model/proxy/selection triplet, but those models resolve data through the active `CPanel`.
   Only the active tab's triplet may be queried or attached to the shared view.
3. Filesystem items are keyed throughout the core, UI, selection state, and plugin API by their deterministic
   `qulonglong` path hash.
4. Slow work returns to the UI through execution queues or typed events; see [threading.md](threading.md) before
   changing asynchronous code.
5. Filesystem links are entries distinct from their targets. `CFileSystemObject::isLink()` includes symlinks and
   Windows junctions but excludes Windows `.lnk` shortcuts. Recursive operations have stricter
   no-follow/ownership rules described in [core-engine.md](core-engine.md).

## Documentation routing

- [core-engine.md](core-engine.md): core ownership, panel/list invariants, filesystem and operation sharp edges.
- [threading.md](threading.md): executors, queueing, lifetime rules, and concurrency review checklist.
- [notifications.md](notifications.md): the listener interfaces, their delivery threads, and the path from a
  navigation command to the refreshed list.
- [qt-ui.md](qt-ui.md): UI ownership and the model/view boundaries.
- [tabs.md](tabs.md): the tab feature where core, UI, notifications, and persistence meet.
- [search.md](search.md): the name-filter and content query language the dialog and the engine share.
- [plugins.md](plugins.md): native plugin ABI, proxy, loader, quick-view ownership, and WCX status.
- [process-launching.md](process-launching.md): every way the app starts a process, the command line's shell and
  output panes, and exiting with commands running.
- [persistence.md](persistence.md): settings ownership and session restoration.
- [build-ci-deps.md](build-ci-deps.md): build entry points, CI, and dependency roles.
- [testing.md](testing.md): test suites, what kinds of tests exist, and which components they cover.
- [coding-style.md](coding-style.md): authoring rules.
- [TODO.md](TODO.md), [code-review-plan.md](code-review-plan.md), and
  [release-metadata-audit.md](release-metadata-audit.md): process and deferred-work documents.
- [planned-refactors/native-paths.md](planned-refactors/native-paths.md): lossless native filesystem paths and
  arbitrary POSIX filename support.
