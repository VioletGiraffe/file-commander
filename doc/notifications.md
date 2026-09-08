# Notifications and the navigation path

Core state changes reach the UI and plugins through listener interfaces, not Qt signals. Every panel listener is
attached to every tab of a side and receives the tab id; a screen-facing implementation filters against the active
tab id (see [tabs.md](tabs.md)). "Deferred" below means the callback is queued and runs on the main thread during a
queue drain; [threading.md](threading.md) lists the queues and who drains them.

## Listener map

| Interface (declared in) | Fires when | Delivery | Implemented by |
|-------------------------|------------|----------|----------------|
| `PanelContentsChangedListener` (`cpanel.h`) | a tab's list is committed (`onPanelContentsChanged`) or withdrawn (`onPanelContentsInvalidated`) | deferred via the tab's UI queue; both share one queue tag, so a commit within the same drain replaces the withdrawal | `CPanelWidget` (refills the model), `CMainWindow` (path bar, title), `CController` (persists tab state) |
| `CurrentItemChangedListener` (`cpanel.h`) | the core designates a tab's cursor item | deferred via the tab's UI queue | `CPanelWidget` |
| `CurrentPathChangedListener` (`cpanel.h`) | a tab's directory changes; not on refresh | synchronous, inside `CPanel::setPath` on the caller's thread | `CController` (visited-locations log) |
| `CController::IVolumeListObserver` (`ccontroller.h`) | the volume list or readiness changes; a side's current volume changes | volume list: deferred via the enumerator's own queue and timer; current volume: synchronous from the controller's navigation call | `CPanelWidget` |
| `IconsChangedListener` (`iconprovider/ciconprovider.h`) | precise icons arrive; every icon is invalidated | deferred via the icon provider's queue | `CPanelWidget` |
| `CController::subscribeToPanelContents` | the active tab's list is committed | from the controller's own `onPanelContentsChanged`, so main thread, under the panel's list mutex; see [plugins.md](plugins.md) | plugin proxies |

## Navigation path

What one `CController::setPath` does, with the thread boundaries marked. Watcher-triggered refreshes and tab
activation take a fresh snapshot and continue from step 3.

1. Main thread, `CController::setPath`: calls `CPanel::setPath`, then records the directory for the side's current
   volume and fires `currentVolumeChanged`.
2. Main thread, `CPanel::setPath`, under the list mutex: resolves the nearest accessible directory (the requested
   path's ancestors, then the previous path, then history, then home), updates back/forward history, fires
   `CurrentPathChangedListener`, arms the watcher if the tab is active, records the cursor for the folder being left
   or entered, and takes the generation/path/mode snapshot. The previous list is withdrawn here
   (`onPanelContentsInvalidated` is queued). An inactive tab stops at this step.
3. Panel worker pool: enumerates the directory into worker-local storage.
4. Worker, `publishFileListIfCurrent`: commits under the list mutex only if generation, path, and mode still match,
   then queues `onPanelContentsChanged`.
5. Main thread, timer drain (`CMainWindow` -> `CController::uiThreadTimerTick`): the listeners run. `CPanelWidget`
   refills the active tab's model from the panel and restores its selection, `CMainWindow` updates the path bar and
   title, `CController` saves tab state, and plugin subscribers receive the committed list.

The rules behind steps 2 and 4 are in [core-engine.md](core-engine.md), "File-list publication".
