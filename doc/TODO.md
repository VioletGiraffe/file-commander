# TODO / backlog

Approved-but-not-started work and consciously deferred items. Current state only; done items get removed.

## Trash support on Linux/FreeBSD

Implement `OsShell::deleteItems` through `QFile::moveToTrash`. F8 currently performs permanent deletion on these
platforms because only Windows and macOS have a native trash backend.

## QoL commands (approved)

### Select / unselect group by mask + select by extension

Follow Total Commander shortcuts: Num+ selects by mask, Num- unselects by mask, and Alt+Num+ selects the current
item's extension. Reuse the existing wildcard filter semantics and omit the synthetic parent entry.

### Go to file location

In flattened show-all-files mode, add a command that opens the selected item's containing folder and selects the
item there.

## Tabs: deferred nice-to-haves

- Locked tabs.
- Drag files from the list onto a tab header to copy/move into that tab's folder without switching to it.
- Persist back/forward history for inactive tabs.
- Make initial column sizing per-tab; the current fresh-install glitch is rare and self-correcting.

Considered and rejected (not worth the complexity at this app's scale) - do not revisit unprompted:
cooperative task-cancellation on tab close.
