# Per-panel tabs

Tabs cross core ownership, UI model/view state, notifications, worker lifetime, and persistence. Start with
`CController` and `CPanelWidget`.

## Identity and ownership

Until controller shutdown, each side owns one or more `CPanel` tabs and an active position. Stable nonzero tab IDs
survive vector growth and reordering; positions do not. They also survive restarts and are never reused, so an id
names one tab for good and persisted state can be keyed by it. Core calls and notifications identify tabs by ID, and
the tab bar stores that ID as tab data. Resolve positions immediately before acting because another operation may
have changed the ordering.

Side-wide listeners attach to every tab. The controller publishes only the active tab to each subscribed plugin
proxy. Visible consumers filter notifications against the active tab ID; folder path is not an identity because
duplicate tabs may show the same location.

## UI invariant

`CPanelWidget` has one shared view and one model/proxy/selection triplet per tab. The models resolve data through
`CController::panel(side)`, so only the active triplet may be queried or attached. Activation swaps the complete
triplet and restores its sort and header state; background notifications cannot touch it.

## Lifetime and persistence

Panels share a worker pool. Every panel task is tagged, and closing a tab retires its tag before destroying panel
state. Only active tabs hold filesystem watches and list their folders: `setPath` on an inactive tab records where it
points and nothing more, so a tab restored from settings is listed the first time it is activated. Activation arms
the watch and lists the folder, which is also how a tab picks up changes made while it was inactive.

`CController` persists one entry per tab -- id, path, cursor hash -- plus the active position and the id counter,
including migration from legacy single-path settings. `CPanelWidget` persists each tab's sort and column layout
against those ids; the recently closed stack is process-local and is not persisted at all. See
[persistence.md](persistence.md) for save timing and history scope.

When changing tab creation, activation, close, or reorder, verify stable-ID mapping, core/UI ordering, active-model
attachment, notification filtering, watcher activation, task retirement, and persistence independently.
