# Builds the app in release and packs it into the installer that setup.iss describes. Requires Inno Setup 6.
# Windows PowerShell 5.1 is enough; create_installer.bat starts this script, the execution policy being what it is.
# The Qt kit is found as Initialize-QtKit in scripts\private\qmake_build.ps1 describes.

$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repositoryRoot 'scripts\private\qmake_build.ps1')
Initialize-QtKit

& (Join-Path $repositoryRoot 'scripts\build_app.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$built = Join-Path $repositoryRoot 'bin\release'
$binaries = Join-Path $PSScriptRoot 'binaries\64'
$qtDeployment = Join-Path $binaries 'Qt'

Remove-Item (Join-Path $PSScriptRoot 'binaries') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $binaries -ErrorAction Stop | Out-Null
# The terminal UI links only QtCore, which the app's deployment below includes
Copy-Item (Join-Path $built 'FileCommander.exe'), (Join-Path $built 'plugin_*.dll'), (Join-Path $built 'fcterm.exe') $binaries -ErrorAction Stop

$windeployqt = Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe'
$deployedBinaries = @(Join-Path $binaries 'FileCommander.exe') + @(Get-ChildItem (Join-Path $binaries 'plugin_*.dll') | ForEach-Object FullName)
$path = $env:PATH
$env:PATH = Join-Path $env:QT_ROOT_DIR 'bin'
try
{
	foreach ($binary in $deployedBinaries)
	{
		& $windeployqt --dir $qtDeployment --force --release --no-translations --no-system-d3d-compiler --no-opengl-sw $binary
		if ($LASTEXITCODE -ne 0) { exit 1 }
	}
}
finally { $env:PATH = $path }

# The MSVC runtime installer gets its own folder: setup.iss runs it during setup instead of installing it with the Qt files
$redistributables = Join-Path $binaries 'vc_redist'
New-Item -ItemType Directory $redistributables -ErrorAction Stop | Out-Null
Move-Item (Join-Path $qtDeployment 'vc_redist.x64.exe') $redistributables -ErrorAction Stop

Remove-Item (Join-Path $qtDeployment '*') -Include 'opengl32sw.dll', 'd3dcompiler_*.dll', 'dx*compiler.dll', 'dxil.dll' -ErrorAction Stop

& (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\iscc.exe') (Join-Path $PSScriptRoot 'setup.iss')
if ($LASTEXITCODE -ne 0) { exit 1 }
exit 0
