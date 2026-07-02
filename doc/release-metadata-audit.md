# Release & identity metadata audit

Findings from a 2026-07-02 metadata review (done while setting up the Darkroom project's release identity,
using File Commander as the reference). Scope: application/vendor identity, version info, installer, license
metadata. Not a code audit — see [code-review-plan.md](code-review-plan.md) for that.

## Identity values currently in use

| Field | Value | Where |
|-------|-------|-------|
| Qt organization name (settings registry path) | `GitHubSoft` | `qt-app/src/main.cpp:23` (applied at :25 via CSettings and :55 via QApplication) |
| Qt application name | `File Commander` | `qt-app/src/main.cpp:22` |
| Installer AppPublisher (visible in Add/Remove Programs) | `VioletGiraffe` | `installer/windows/setup.iss:5` |
| Installer AppCopyright | `VioletGiraffe` | `setup.iss:18` |
| .rc CompanyName | `GithubSoft` (third spelling) | `qt-app/resources/file_commander.rc:13` |
| Installer AppId | `FileCommander` (plain string, not GUID) | `setup.iss:3` |
| Executable / display naming | `FileCommander.exe` (no space) / `File Commander` (space) | `setup.iss:10,16` |
| Live version number | `0.9.9.7` in `VERSION_STRING` | `qt-app/src/version.h:9` |
| License | Apache-2.0 | repo-root `LICENSE` |

The vendor identity is three-way inconsistent (`GitHubSoft` / `GithubSoft` / `VioletGiraffe`). Only
`VioletGiraffe` ever appears where users look (installer, copyright, GitHub); the other two live in
registry paths and metadata fields. Newer projects (Darkroom) standardize on `VioletGiraffe` everywhere.
Changing FC's org name now would orphan existing users' settings (registry store moves), so it stays.

## Version info: three sources, all drifted

1. `qt-app/src/version.h:9` — `0.9.9.7`. The live one: feeds the about dialog
   (`qt-app/src/aboutdialog/caboutdialog.cpp:13`) and the github-releases-autoupdater version comparison
   (`qt-app/src/cmainwindow.cpp:106,814`). Bumped per release.
2. `qt-app/resources/file_commander.rc:6-7,14,16` — `FILEVERSION 1` / `PRODUCTVERSION 1` (malformed:
   should be 4-part `x,y,z,w`) and `"1.0"` strings. Stale since ~2013; this is what Explorer's file
   Properties dialog shows.
3. `installer/windows/setup.iss` — no version at all: no `AppVersion`, and `AppVerName` (:4) is
   versionless. Add/Remove Programs shows no version — notably odd for an app with a built-in updater.

Cause of #2: `qt-app/qt-app.pro` sets `RC_FILE = resources/file_commander.rc`, which disables qmake's
auto-generated-.rc facility entirely — `VERSION` and `QMAKE_TARGET_COMPANY/PRODUCT/DESCRIPTION/COPYRIGHT`
would otherwise generate all of this from the .pro.

Partially fixed 2026-07-02: qt-app.pro now parses `VERSION` out of `version.h` at qmake time
(`$$cat` + `$$find` + `$$replace` near the top of the .pro; the define must stay a plain one-line string
literal, and no other line in version.h may contain the token it greps for). Still remaining for full
single-sourcing: drop `RC_FILE`, move the icon to `RC_ICONS`, set `QMAKE_TARGET_*` (fixes all .rc defects
below in one stroke), and have the installer read the number from the built exe via
`GetVersionNumbersString(...)` in the .iss preprocessor (done in Darkroom's installer.iss as a working
example). Caveat inherent to the parse: editing version.h does not re-run qmake, so locally-built version
metadata lags until the next qmake run; CI is unaffected (always runs qmake fresh).

## .rc content defects (`qt-app/resources/file_commander.rc`)

- `:19` `LegalCopyright "Copyright � 2013 VioletGIraffe"` — mojibake where the (c) symbol should be
  (encoding mishap: .rc is ANSI, the symbol was written in another encoding), `GIraffe` typo, stale year.
- `:22` `OriginalFilename "File Commander"` — wrong twice: missing `.exe`, and the actual exe has no space
  (`FileCommander.exe`).
- `:20-21` `LegalTrademarks1/2 "All Rights Reserved"` — copyright boilerplate in a trademark field, and
  contradicts the Apache-2.0 license.
- `:6-7` malformed single-part `FILEVERSION` / `PRODUCTVERSION` (see above).
- `:13` `GithubSoft` capitalization inconsistent with `main.cpp`'s `GitHubSoft`.

## Installer hardening (`installer/windows/setup.iss`)

- No `ArchitecturesAllowed`. `ArchitecturesInstallIn64BitMode=x64` is set (:21), but on a 32-bit system the
  installer would still run and install x64 binaries that cannot start. Modern form: the
  `ArchitecturesAllowed=x64compatible` + `ArchitecturesInstallIn64BitMode=x64compatible` pair.
- `:6` `DefaultDirName={pf}` — deprecated constant; `{autopf}` is the current form. Cosmetic.
- No `AppVersion` (covered above).
- Apache-2.0 section 4 applies to binary distribution: the installer should ship `LICENSE` (and a `NOTICE`
  file, if one is added — none exists as of this audit) into `{app}`.

## Do NOT "fix"

- **`AppId=FileCommander` must stay exactly as-is.** A plain-string AppId is legal in Inno; its only job is
  to never change (it keys the uninstall registry entry and upgrade-install matching). Switching it to a
  GUID now would orphan every existing install — the very failure it exists to prevent. String AppIds are
  a collision risk ("File Commander" is a name other products use) and embed the product name (a rename
  hazard), which is why new projects should start with a GUID — but existing ones are grandfathered.
- Qt org name `GitHubSoft` (settings store path) — same reasoning: changing it orphans user settings unless
  a migration is shipped. Cosmetic-only benefit; not worth it without one.

## What's already right

Repo-root Apache-2.0 `LICENSE` (and per-submodule licenses); an explicit `AppId` (many .iss files lack one
and silently key on `AppName`); a single live version constant wired to both the updater and the about
dialog; `UsePreviousAppDir` + `SetupIconFile` + `UninstallDisplayIcon` present; modern `WizardStyle`.
