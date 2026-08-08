# Per-panel tabs

Tabs cross core ownership, UI model/view state, notifications, worker lifetime, and persistence. Start with
`CController` and `CPanelWidget`.

## Identity and ownership

Until controller shutdown, each side owns one or more `CPanel` tabs and an active position. Stable nonzero tab IDs
survive vector growth and reordering; positions do not. Core calls and notifications identify tabs by ID, and the
tab bar stores that ID as tab data. Resolve positions immediately before acting because another operation may have
changed the ordering.

Side-wide listeners attach to every tab. The controller publishes only the active tab to the plugin proxy. Visible
consumers filter notifications against the active tab ID; folder path is not an identity because duplicate tabs may
show the same location.

## UI invariant

`CPanelWidget` has one shared view and one model/proxy/selection triplet per tab. The models resolve data through
`CController::panel(side)`, so only the active triplet may be queried or attached. Activation swaps the complete
triplet and restores its sort and header state; background notifications cannot touch it.

## Lifetime and persistence

Panels share a worker pool. Every panel task is tagged, and closing a tab retires its tag before destroying panel
state. Only active tabs hold filesystem watches; activation re-arms the watch and refreshes changes missed while
inactive.

`CController` persists paths, active position, and cursor hashes, including migration from legacy single-path
settings. UI column state and the process-local recently closed stack remain in `CPanelWidget`. See
[persistence.md](persistence.md) for save timing and history scope.

When changing tab creation, activation, close, or reorder, verify stable-ID mapping, core/UI ordering, active-model
attachment, notification filtering, watcher activation, task retirement, and persistence independently.
