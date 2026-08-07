# Persistence and settings

`CSettings` wraps `QSettings`; the application and organization names set in `main.cpp` determine the platform
store. Core keys live in `file-commander-core/include/settings.h`.

## Ownership

`CController` owns persistent navigation state:

- all tab paths and the active position;
- each tab's cursor hash;
- the active tab's back/forward history;
- the side-wide visited-location log.

Legacy single-path settings migrate to one tab. Tab state is saved after committed panel updates and at shutdown,
with duplicate signatures suppressed so watcher refreshes do not rewrite unchanged settings. History is saved
periodically and at shutdown rather than on every navigation.

UI-only state remains outside the controller. `CPanelWidget` owns column header state, and the main window or
individual widgets own their geometry and splitter state. See [tabs.md](tabs.md) for the identity and lifetime
boundaries between core tabs and UI tab state.
