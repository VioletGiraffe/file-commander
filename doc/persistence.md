# Persistence and settings

`CSettings` wraps `QSettings`; the application and organization names set in `main.cpp` determine the platform
store. Core keys live in `file-commander-core/include/settings.h`.

## Ownership

`CController` owns persistent navigation state:

- one entry per tab holding its id, path and cursor hash;
- the active position, and the counter tab ids are drawn from;
- the active tab's back/forward history;
- the side-wide visited-location log.

Legacy single-path settings migrate to one tab. Tab state is saved after committed panel updates and at shutdown,
with duplicate signatures suppressed so watcher refreshes do not rewrite unchanged settings. History is saved
periodically and at shutdown rather than on every navigation.

UI-only state remains outside the controller. `CPanelWidget` persists every tab's sort and column layout under its
own key, written once at close, and the main window or individual widgets own their geometry and splitter state.
That store is keyed by tab id, not by tab position, so it stays correct against a controller store written at a
different time: an unknown id is ignored and a tab with no entry falls back to defaults. Ids are never reused, so a
stale entry is stale, never wrong. See [tabs.md](tabs.md) for the identity and lifetime
boundaries between core tabs and UI tab state.
