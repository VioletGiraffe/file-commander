# Per-panel tabs

Tabs cross core ownership, UI model/view state, notifications, worker lifetime, and persistence. Start with
`CController` and `CPanelWidget`.

## Identity and ownership

Until controller shutdown, each side owns one or more `CPanel` tabs and an active position. Stable nonzero tab IDs
survive vector growth and reordering; positions do not. They also survive restarts and are never reused, so an id
names one tab for good and persisted state can be keyed by it. IDs are random 64-bit numbers: instances sharing the
settings, the GUI and the terminal UI included, may run at the same time, and a shared counter would hand them the
same ids. Core calls and notifications identify tabs by ID, and the tab bar stores that ID as tab data. Resolve
positions immediately before acting because another operation may have changed the ordering.

Side-wide listeners attach to every tab. The controller publishes only the active tab to each subscribed plugin
proxy. Visible consumers filter notifications against the active tab ID; folder path is not an identity because
duplicate tabs may show the same location.

## UI invariant

`CPanelWidget` has one shared view and one model and selection model per tab. A model holds a copy of its rows,
refilled only while its tab is on screen. Activation swaps the models and restores their column layout; background
notifications cannot touch them. Each tab's `CPanel` holds its sorting, name filter and selection; the model and
the selection model show copies. The side's one filter box is shown whenever the active tab is filtered.

The terminal UI's `CTuiPanel` keeps no state per tab: switching refills its one list from the active tab's `CPanel`,
and its filter line follows the same rule as the Qt filter box.

## Lifetime and persistence

Panels share the controller's thread pools. Every panel task is tagged, and closing a tab retires its tag before
destroying panel state. Only active tabs hold filesystem watches and list their folders: `setPath` on an inactive tab
records the path as given, without reading the disk, so a tab restored from settings is checked and listed the first
time it is activated. Activation arms the watch and lists the folder, which is also how a tab picks up changes made
while it was inactive.

`CController` persists one entry per tab -- id, path, cursor hash, sorting -- plus the active position, including
migration from legacy single-path settings. Its stack of each side's recently closed tab folders is not persisted.
`CPanelWidget` persists each tab's column layout against those ids. See [persistence.md](persistence.md) for save
timing and history scope.

When changing tab creation, activation, close, or reorder, verify stable-ID mapping, core/UI ordering, active-model
attachment, notification filtering, watcher activation, task retirement, and persistence independently.
