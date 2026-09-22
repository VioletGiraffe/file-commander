# Builds file-commander-core\core-tests and runs every test executable, then reports which suites failed.
# Exit code: 1 if any suite failed or was not built, 2 when the environment is incomplete.
# Windows PowerShell 5.1 is enough; run_tests.bat starts this script, the execution policy being what it is.
#
# The Qt kit comes from QT_ROOT_DIR, or from a git-ignored local-env.ps1 beside this script.
# MSBuild and qmake need the MSVC environment: qmake probes cl for the compiler version, and gates both the flags
# and the toolset it writes into the projects on it, so the build uses the toolset in the projects qmake wrote.

# No positional binding: every parameter is named, so that a Catch2 spec cannot bind to one by position
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('release', 'debug')]
	[string]$Configuration = 'release',

	# Build without running, for a CI job that deploys between the two; -NoBuild runs what is already built
	[switch]$BuildOnly,
	[switch]$NoBuild,

	# Also run the suites' slow cases, the ones listed with a filter below
	[switch]$All,

	# Run only the suites whose executable name contains this
	[string]$Suite,

	# Arguments for the selected suite, e.g. a Catch2 test spec or --std-seed
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$SuiteArguments
)

# Every suite, with the arguments it runs with by default. The filtered-out cases generate trees of thousands of
# files and gigabytes of data; CI runs them separately with fresh seeds. -All clears the filters.
$suites = [ordered]@{
	'fso_test'                = @()
	'fso_test_high_level'     = @()
	'panel_test'              = @()
	'filesearchengine_test'   = @()
	'userprograms_test'       = @()
	'filesystemhelpers_test'  = @()
	'fileoperations_test'     = @('~[executor]~[deleteexecutor]')
	'filecomparator_test'     = @('~[CFileComparator]')
	'fileoperations_gui_test' = @()
	'filelist_test'           = @()
	'csvviewer_test'          = @()
}

function Fail-Environment([string]$message)
{
	[Console]::Error.WriteLine($message)
	exit 2
}

$repositoryRoot = Split-Path $PSScriptRoot -Parent

# The Qt kit, the directory holding bin\qmake.exe. Its bin goes on PATH: the test executables need the Qt DLLs.
if (-not $env:QT_ROOT_DIR)
{
	$localEnvironment = Join-Path $PSScriptRoot 'local-env.ps1'
	if (Test-Path $localEnvironment) { . $localEnvironment }
}
if (-not $env:QT_ROOT_DIR)
{
	Fail-Environment "QT_ROOT_DIR is not set: set it to the Qt kit directory, the one holding bin\qmake.exe, or set it in a git-ignored local-env.ps1 beside this script."
}
$qmake = Join-Path $env:QT_ROOT_DIR 'bin\qmake.exe'
if (-not (Test-Path $qmake)) { Fail-Environment "No qmake.exe under `"$env:QT_ROOT_DIR\bin`"." }
$env:PATH = (Join-Path $env:QT_ROOT_DIR 'bin') + ';' + $env:PATH

if (-not $NoBuild)
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
	Get-ChildItem $repositoryRoot -Recurse -Force -Filter '.qmake.stash' -ErrorAction SilentlyContinue | Remove-Item -Force

	Push-Location (Join-Path $repositoryRoot 'file-commander-core\core-tests')
	try
	{
		& $qmake -tp vc -r
		if ($LASTEXITCODE -ne 0) { exit 1 }
		# msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
		msbuild /t:Build /nologo /m /v:minimal "/p:Configuration=$Configuration" core-tests.sln
		if ($LASTEXITCODE -ne 0) { exit 1 }
	}
	finally { Pop-Location }
}

if ($BuildOnly) { exit 0 }

$binaries = Join-Path $repositoryRoot "bin\$Configuration"
$ran = @()
$failed = @()

foreach ($name in $suites.Keys)
{
	if ($Suite -and $name -notlike "*$Suite*") { continue }
	$ran += $name

	$executable = Join-Path $binaries "$name.exe"
	if (-not (Test-Path $executable))
	{
		Write-Host "${name}: not built"
		$failed += $name
		continue
	}

	# Typed: a one-element array unrolls to a string on its way out of the if, and splatting a string passes its
	# characters one argument each.
	[string[]]$arguments = if ($SuiteArguments) { $SuiteArguments } elseif ($All) { @() } else { $suites[$name] }
	Write-Host "`n===== $name"
	# --warn NoTests: a filter that matches nothing exits 0 otherwise, so a broken one would pass silently
	& $executable @arguments --warn NoTests
	if ($LASTEXITCODE -ne 0) { $failed += $name }
}

if (-not $ran) { Fail-Environment "No test executable matches `"$Suite`"." }
if ($failed)
{
	Write-Host "`nFAILED: $($failed -join ' ')"
	exit 1
}
Write-Host "`nAll suites passed: $($ran -join ' ')"
exit 0
