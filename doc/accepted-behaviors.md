# Accepted behaviors

Edge behaviors and gaps that were reviewed and accepted. Reviews do not report them again unless a code change
invalidates the reason given.

## Filesystem watchers

- A filesystem driver that accepts a change watch but never delivers notifications leaves its folders without
  automatic refresh: no API reveals it, and the defect is the driver's. Handling is added only for a driver met in practice.
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
