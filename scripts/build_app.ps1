# Builds File Commander, its plugins and the terminal UI into bin\<configuration>.
# Exit code: 1 if the build failed, 2 when the environment is incomplete.
# Windows PowerShell 5.1 is enough; build_app.bat starts this script, the execution policy being what it is.
# The Qt kit is found as Initialize-QtKit in private\qmake_build.ps1 describes.

[CmdletBinding()]
param(
	[ValidateSet('release', 'debug')]
	[string]$Configuration = 'release'
)

. (Join-Path $PSScriptRoot 'private\qmake_build.ps1')
Initialize-QtKit
Build-QmakeProject (Join-Path (Split-Path $PSScriptRoot -Parent) 'file-commander.pro') $Configuration
exit 0
