# File Commander architecture notes

These documents orient an agent before it reads the code. They record cross-file structure, invariants, and
surprising behavior; the source remains authoritative for APIs and current feature lists.

## Project shape

File Commander is a cross-platform orthodox dual-panel file manager. Windows is the primary target; macOS and
Linux are supported, and FreeBSD is best-effort where the Linux implementation works unchanged. The project uses
C++23, Qt 6.8+, and qmake.

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

## Source map

| Area | Start here |
|------|------------|
| Core facade and tabs | `file-commander-core/src/ccontroller.{h,cpp}` |
| One tab's directory state | `file-commander-core/src/cpanel.{h,cpp}` |
| Filesystem entry wrapper | `file-commander-core/src/cfilesystemobject.{h,cpp}` |
| Copy, move, permanent delete | `file-commander-core/src/fileoperations/` and `fileoperations.pri` |
| File and content search | `file-commander-core/src/filesearchengine/`, `qt-app/src/filessearchdialog/` |
| Main window and command routing | `qt-app/src/cmainwindow.{h,cpp,ui}` |
| Panel UI and file-list MVC | `qt-app/src/panel/` |
| Native plugin API and loader | `file-commander-core/src/plugininterface/`, `pluginengine/` |
| Shipped plugins | `plugins/viewer/`, `plugins/tools/` |
| Build and test graph | `file-commander.pro`, `file-commander-core/core-tests/core-tests.pro` |

Top-level dependency directories (`qtutils`, `cpputils`, `cpp-template-utils`, `thin_io`,
`text-encoding-detector`, `image-processing`, and `github-releases-autoupdater`) are Git submodules and separate
repositories.

## Invariants to carry into code reading

1. Each side always owns at least one tab; a tab owns a `CPanel`. `CController::panel(side)` returns the active
   tab, while tab IDs remain stable across reordering.
2. Each UI tab owns a model/proxy/selection triplet, but those models resolve data through the active `CPanel`.
   Only the active tab's triplet may be queried or attached to the shared view.
3. Filesystem items are keyed throughout the core, UI, selection state, and plugin API by their deterministic
   `qulonglong` path hash.
4. Core-to-UI notification uses listener interfaces. Slow work returns through execution queues or typed event
   queues; see [threading.md](threading.md) before changing asynchronous code.
5. Filesystem links are entries distinct from their targets. `CFileSystemObject::isLink()` includes symlinks and
   Windows junctions but excludes Windows `.lnk` shortcuts. Recursive operations have stricter
   no-follow/ownership rules described in [core-engine.md](core-engine.md).

## Documentation routing

- [core-engine.md](core-engine.md): core ownership, panel/list invariants, filesystem and operation sharp edges.
- [threading.md](threading.md): executors, queueing, lifetime rules, and concurrency review checklist.
- [qt-ui.md](qt-ui.md): UI ownership and the model/view boundaries.
- [tabs.md](tabs.md): the tab feature where core, UI, notifications, and persistence meet.
- [search.md](search.md): the name-filter and content query language the dialog and the engine share.
- [plugins.md](plugins.md): native plugin ABI, proxy, loader, quick-view ownership, and WCX status.
- [persistence.md](persistence.md): settings ownership and session restoration.
- [build-ci-deps.md](build-ci-deps.md): build/test entry points and dependency roles.
- [oddities.md](oddities.md): unresolved defects and non-obvious hazards.
- [coding-style.md](coding-style.md): authoring rules.
- [TODO.md](TODO.md), [code-review-plan.md](code-review-plan.md), and
  [release-metadata-audit.md](release-metadata-audit.md): process and deferred-work documents.
- [planned-refactors/native-paths.md](planned-refactors/native-paths.md): lossless native filesystem paths and
  arbitrary POSIX filename support.
