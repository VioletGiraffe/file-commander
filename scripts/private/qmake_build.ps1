# Dot-sourced by the scripts that build with qmake. Windows PowerShell 5.1 is enough.
# The functions exit the calling script on failure: with code 1 for a failed build, 2 when the environment is incomplete.

function Fail-Environment([string]$message)
{
	[Console]::Error.WriteLine($message)
	exit 2
}

# Takes the Qt kit, the directory holding bin\qmake.exe, from QT_ROOT_DIR, or from a git-ignored scripts\local-env.ps1.
# Its bin goes on PATH: the built executables need the Qt DLLs.
function Initialize-QtKit
{
	if (-not $env:QT_ROOT_DIR)
	{
		$localEnvironment = Join-Path (Split-Path $PSScriptRoot -Parent) 'local-env.ps1'
		if (Test-Path $localEnvironment) { . $localEnvironment }
	}
	if (-not $env:QT_ROOT_DIR)
	{
		Fail-Environment "QT_ROOT_DIR is not set: set it to the Qt kit directory, the one holding bin\qmake.exe, or set it in a git-ignored scripts\local-env.ps1."
	}
	if (-not (Test-Path (Join-Path $env:QT_ROOT_DIR 'bin\qmake.exe'))) { Fail-Environment "No qmake.exe under `"$env:QT_ROOT_DIR\bin`"." }
	$env:PATH = (Join-Path $env:QT_ROOT_DIR 'bin') + ';' + $env:PATH
}

# Builds the qmake project $proFile and its subprojects with MSBuild, in the project's folder. Requires Initialize-QtKit.
# MSBuild and qmake need the MSVC environment: qmake probes cl for the compiler version, and gates both the flags
# and the toolset it writes into the projects on it, so the build uses the toolset in the projects qmake wrote.
function Build-QmakeProject([string]$proFile, [ValidateSet('release', 'debug')][string]$configuration)
{
	if (-not (Get-Command cl -ErrorAction SilentlyContinue))
	{
		$installerDirectory = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
		$vswhere = Join-Path $installerDirectory 'vswhere.exe'
		if (-not (Test-Path $vswhere)) { Fail-Environment 'vswhere.exe was not found. Install Visual Studio with the C++ toolset.' }

		$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
			Select-Object -First 1
		if (-not $visualStudio) { Fail-Environment 'No Visual Studio with the C++ toolset was found.' }

		# vcvars64.bat itself calls a bare vswhere.exe, which it expects on PATH
		$env:PATH += ';' + $installerDirectory
		# A child process's environment is not inherited back, so vcvars64.bat's is read out of cmd and imported
		$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
		cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
			if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
		}
	}

	# A kept .qmake.stash pins the toolchain probed when it was written, so every run probes the current one instead.
	# One is written per subproject build directory, and qmake also searches upward, so the sweep covers the repository.
	Get-ChildItem (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Recurse -Force -Filter '.qmake.stash' -ErrorAction SilentlyContinue | Remove-Item -Force

	$proFileName = Split-Path $proFile -Leaf
	Push-Location (Split-Path $proFile -Parent)
	try
	{
		& (Join-Path $env:QT_ROOT_DIR 'bin\qmake.exe') -tp vc -r $proFileName
		if ($LASTEXITCODE -ne 0) { exit 1 }
		# msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
		msbuild /t:Build /nologo /m /v:minimal "/p:Configuration=$configuration" ([IO.Path]::ChangeExtension($proFileName, 'sln'))
		if ($LASTEXITCODE -ne 0) { exit 1 }
	}
	finally { Pop-Location }
}
