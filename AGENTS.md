# File Commander project guidance

## Project essentials

- File Commander is a cross-platform orthodox dual-panel file manager. Windows is the primary target; macOS, Linux, and FreeBSD are also supported.
- The codebase is C++23 with Qt 6.8+ and uses qmake (`file-commander.pro` plus `.pro`/`.pri` files), not CMake.
- The main dependency direction is `qt-app/` (Qt Widgets GUI) -> `file-commander-core/` (controller, panels, filesystem and operations). Native plugins under `plugins/` depend on the core interface but are loaded dynamically. Keep UI access to panels and filesystem objects behind `CController`.
- Several top-level dependency directories are Git submodules. Treat them as separate repositories and do not modify them unless the task explicitly targets them.

## Documentation routing

- Start with `doc/README.md`, then read the documents relevant to the change. The documentation is an architecture map, not a substitute for checking the current code.
- Core, filesystem, file operations, or concurrency: `doc/core-engine.md`, `doc/threading.md`, and `doc/oddities.md`.
- GUI or tabs: `doc/qt-ui.md` and `doc/tabs.md`.
- Settings or session restoration: `doc/persistence.md`.
- Plugins: `doc/plugins.md`.
- Build, tests, CI, or dependencies: `doc/build-ci-deps.md`.
- Installer, version, vendor, license, or other release identity: `doc/release-metadata-audit.md`.
- Do not build or compile the project; the user performs build verification.

## Invariants to preserve

- Each side always has at least one tab, and each tab owns a `CPanel`. `CController::panel(side)` returns that side's active tab. Tabs have stable IDs independent of their display positions.
- Each UI tab has its own model/proxy/selection triplet, but those models resolve data through the active `CPanel`; only the active tab's triplet may be queried or attached to the shared view.
- Filesystem items are identified throughout the core, UI, selection state, and plugin API by their deterministic `qulonglong` path hash, not directly by path.
- Core-to-UI notifications use listener/observer interfaces. Slow work runs off the UI thread and returns through execution queues or buffered observer callbacks.
- All `CPanel` work posted to the shared panel worker pool must carry the panel's task tag. Panel destruction retires that tag; preserve this lifetime guarantee when adding asynchronous work.
- Filesystem links are entries distinct from their targets. Use `CFileSystemObject::isLink()` when that distinction matters, and ensure delete/move operations on a link cannot affect the target. Read the traversal rules in `doc/core-engine.md` before changing recursive operations.
- Existing settings keys and release identity values are compatibility contracts. Consult the persistence and release-metadata documents before renaming or normalizing them; several intentionally retained values look misspelled or inconsistent.
