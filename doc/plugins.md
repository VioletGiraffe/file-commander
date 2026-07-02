# Plugin system

Two independent mechanisms:

1. **Native File Commander plugins** — dynamic libs implementing the `CFileCommanderPlugin` interface,
   loaded by `CPluginEngine`. The three shipped plugins use this.
2. **WCX archive plugins** — Total Commander `.wcx` archive plugins, hosted by `CWcxPluginHost` (Windows only).

## Native plugin interface (`core/src/plugininterface/`)

`PLUGIN_EXPORT` (from `plugin_export.h`, gated by `PLUGIN_MODULE` define) marks the exported symbols.

- **`CFileCommanderPlugin`** (base) — `enum PluginType { Viewer, Archive, Tool }`; pure virtual `type()` and
  `name()`. `setProxy(CPluginProxy*)` is called by the engine after load; `proxySet()` is the init hook
  (plugins build their UI / register menu entries there). Holds `_proxy`.
- Each plugin DLL exports **`extern "C" CFileCommanderPlugin* createPlugin()`** — the only required symbol.
- **`CFileCommanderViewerPlugin`** — adds `bool canViewFile(fileName, QMimeType)` and
  `WindowPtr<CPluginWindow> viewFile(fileName)`; `type()` returns `Viewer`.
- **`CFileCommanderToolPlugin`** — `type()` returns `Tool`; tools add Tools-menu entries via the proxy.
- **`CPluginWindow`** — base for plugin-provided windows (viewer / diff windows).
- **`WindowPtr<T>`** — `unique_ptr<T, void(*)(CPluginWindow*)>` with a **custom deleter that deletes inside
  the plugin's own DLL** (`WindowPtr::create` captures a deleter lambda compiled in the plugin module). This
  is the standard cross-DLL allocate/free-in-the-same-module discipline; the default-constructed form holds
  null with a no-op deleter.

## CPluginProxy (`core/src/plugininterface/cpluginproxy.{h,cpp}`)

The API surface a plugin uses to talk back to the app (the plugin never sees `CController` directly).

- Holds a snapshot `std::array<PanelState,2>` where `PanelState = { FileListHashMap panelContents,
  vector<qulonglong> selectedItemsHashes, qulonglong currentItemHash, QString currentFolder }`. Panel sides
  are the plugin-facing enum `PanelPosition { PluginLeftPanel, PluginRightPanel, PluginUnknownPanel }`.
- **Updates pushed in by core/UI:** `panelContentsChanged`, `selectionChanged`, `currentItemChanged`,
  `currentPanelChanged`.
- **Queries for plugins:** `currentPanel`/`otherPanel`, `panelState`, `currentFolderPathForPanel`,
  `currentItemPathForPanel`, `currentItemForPanel`, `currentItem`, `currentItemPath`.
- **Tool menu:** `createToolMenuEntries(MenuTree)` — a `MenuTree` is a recursive `{name, icon, handler,
  children}`; the UI (`CMainWindow::createToolMenuEntries`) renders it into the real Tools menu. Each plugin
  is expected to call this once.
- **`execOnUiThread(code)`** — the proxy is constructed with a UI-thread executor (wired to
  `CController::execOnUiThread`) so plugins can marshal work to the UI thread.

## CPluginEngine (`core/src/pluginengine/cpluginengine.{h,cpp}`)

Singleton (`CPluginEngine::get()`), a `PanelContentsChangedListener` attached to panels.

- **Discovery/loading (`loadPlugins`):** scans `qApp->applicationDirPath()` for files matching
  `*plugin_*<ext>*` where `<ext>` = `.dll` (Win) / `.so` (Linux/FreeBSD) / `.1.0.0.dylib` (macOS); skips
  symlinks; `QLibrary::resolve("createPlugin")`; on success `setProxy(&CController::get().pluginProxy())`
  and stores `pair<unique_ptr<plugin>, unique_ptr<QLibrary>>` (library kept alive for the plugin's lifetime).
- Forwards `onPanelContentsChanged` / `selectionChanged` / `currentItemChanged` / `currentPanelChanged` into
  the proxy (translating `Panel` -> `PanelPosition`).
- **Viewer dispatch:** `viewerForCurrentFile` / `createViewerWindowForCurrentFile` pick the first viewer
  whose `canViewFile` accepts the current item; `viewCurrentFile` opens it (full window or quick-view).

## WCX archive host (`core/src/plugininterface/wcx/`)

- **`CWcxPluginHost`** (Windows) — `setWcxSearchPath(path)` then `loadPlugin` each `.wcx` (`QLibrary`,
  stored in a `std::deque` because `QLibrary` isn't movable). `wcxhead.h` is the Total Commander WCX C API.
  Owned by `CController` as `_wcxHost`.
- **`cwcxpluginhost_stub.h`** — no-op stand-in compiled on non-Windows (`#ifdef _WIN32` in `ccontroller.h`).
- Status: host/loader present; full browse-archive-as-folder integration is partial. Confirm against code
  before assuming archive contents are mountable into a panel.

## Shipped plugins (`plugins/`)

Built before `qt-app`. Output names `plugin_<x>` (e.g. `libplugin_imageviewer.so.1.0.0`,
`plugin_imageviewer.dll`) so they match the engine's `*plugin_*` glob.

| Plugin | Path | Type | Notes |
|--------|------|------|-------|
| Image viewer | `plugins/viewer/imageviewer` | Viewer | `CImageViewerPlugin` + `CImageViewerWidget`/`Window`. Backed by the **image-processing** submodule. |
| Text viewer | `plugins/viewer/textviewer` | Viewer | `CTextViewerPlugin` + `CTextViewerWindow`, `CLightningFastViewer`, `CTextEditWithImageSupport`, `CFindDialog`. Encoding detection via **text-encoding-detector** submodule; syntax highlighting via vendored **qutepart-cpp** (`3rdparty/diegoiast/qutepart-cpp`). |
| File comparison | `plugins/tools/filecomparisonplugin` | Tool | `CFileComparisonPlugin` — adds a Tools-menu entry; compares the two selected files via core `CFileComparator`, shows a `CSimpleProgressDialog`. |

Each `.pro` declares its `depends` on `file_commander_core` + needed submodules (see top-level
`file-commander.pro`).

See [core-engine.md](core-engine.md) for `CFileComparator`, [qt-ui.md](qt-ui.md) for quick-view hosting
(`CPanelDisplayController`).
