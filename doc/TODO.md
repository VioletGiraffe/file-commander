# TODO / backlog

Approved-but-not-started work, consciously deferred items, and known defects. Current state only; done items get
removed.

## QoL commands (approved)

Chosen from a longer list; no other command from it is approved.

### Go to file location

In flattened show-all-files mode, add a command that opens the selected item's containing folder and selects the
item there.

## Tabs: deferred nice-to-haves

- Locked tabs.
- Drag files from the list onto a tab header to copy/move into that tab's folder without switching to it.
- Persist back/forward history for inactive tabs.
- Make initial column sizing per-tab; the current fresh-install glitch is rare and self-correcting.

## File operations

- Conflict planning inspects each item under a merged destination folder individually (one to three stats per
  item, repeated at execution). One directory listing per merged folder would replace the per-item stats.
- Automatic bulk renaming in the conflict plan (`name (2).ext`), also the natural answer for same-name roots
  selected from a flattened view, which planning leaves to a live prompt today.

## Folder synchronization

Folder comparison exists (`file-operations/foldercomparison/`, `qt-app/src/tools/cfoldercomparisonwindow`);
synchronization does not. It is a core feature, not a plugin (see [plugins.md](plugins.md)): it builds a plan, a list
of copies, moves and deletions, and hands it to the file-operation engine, which already runs batches with progress
and conflict prompts.

Open decisions, to settle before building:
- Default comparison criteria: size, modification time or content. Modification times are unreliable across
  filesystems; content is authoritative but slow.
- Direction: mirror one side to the other, newer wins, or per row. A mirror deletes the entries only the destination
  has, which needs guarding.
- UI: an aligned two-column view with a direction and a checkbox per row, or a flat list of differences.

## Visited-locations merge

Its design has open questions for the maintainer to settle; not to be changed until then.
