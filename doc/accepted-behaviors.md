# Accepted behaviors

Edge behaviors and gaps that were reviewed and accepted. Reviews do not report them again unless a code change
invalidates the reason given.

## Filesystem watchers

- A filesystem driver or file server that accepts a change watch but never delivers notifications leaves its folders
  without automatic refresh: no API reveals it, and the defect is the driver's or the server's. Handling is added only
  for one met in practice.
  - Windows network shares are therefore not polled: their servers report every client's changes, and polling would
    list the folder over the network every 2 s.
- On macOS a new watch can report a change made shortly before it started, refreshing the panel once more: FSEvents
  delivers such a change after the stream starts, and no API waits for it. Panel tests wait for their trees' changes to
  be reported before a panel watches anything (`waitUntilChangesAreReported()` in `paneltesthelpers.h`).
- A folder renamed away and replaced under its name keeps the watch on the old folder when the watch cannot follow the
  folder's own name (Windows: an unreadable parent; Linux: an unreadable ancestor moved). Coverage is partial there, so
  the poller follows the path: the panel stays correct, with polling latency, until it leaves the folder.
- Navigating to a polled folder lists it twice, for the poller's baseline (`captureBaselineState()`) and for the
  panel: sharing one listing needs a new thin_io API and a second way to set the baseline, and a polled folder is
  listed every 2 s anyway.
- A tab keeps its parked `CDirectorySnapshotPoller` thread from its first poll until the tab closes: only folders
  without dependable notifications are polled, so few tabs ever start one.

## Content search

- A regex match longer than the 4 KiB margin around each scan window is not found where it crosses the window's edge
  (`fileContentsMatches`): a regex match has no bounded length, and a query that long is not a real use. One subject per
  file and PCRE2 partial matching were considered and declined.

## Tabs

- Closing a tab does not cancel its tasks cooperatively: not worth the complexity at this app's scale.
- Every UI tab creates its models up front (`CPanelWidget::PanelTab`); creating them on first display was declined.

## Locking

- `CPanel::setPath` holds the panel mutex across blocking calls: see [threading.md](threading.md).
- Path-change listeners run under the panel mutex: see `CController::warnIfVisitedLocationDropped`.
- `CVolumeEnumerator` notifies its observers while holding `_mutexForDrives`: see the member's comment.

## Tests

- `CLightningFastViewerWidget` (text viewer plugin) has no test target: its column, wrap and search arithmetic is
  verified by inspection.
