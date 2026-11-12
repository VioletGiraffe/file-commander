# Builds file-commander-core\core-tests and runs every test executable, then reports which suites failed.
# Exit code: 1 if any suite failed or was not built, 2 when the environment is incomplete.
# Windows PowerShell 5.1 is enough; run_tests.bat starts this script, the execution policy being what it is.
# The Qt kit is found as Initialize-QtKit in private\qmake_build.ps1 describes.

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
	'controller_test'         = @()
	'filesearchengine_test'   = @()
	'batchrename_test'        = @()
	'userprograms_test'       = @()
	'filesystemhelpers_test'  = @()
	'shell_test'              = @()
	'fileoperationlaunch_test' = @()
	'fileoperations_test'     = @('~[executor]~[deleteexecutor]')
	'foldercomparison_test'   = @('~[CFileComparator]')
	'fileoperationsui_test'   = @()
	'fileoperations_gui_test' = @()
	'filelist_test'           = @()
	'csvviewer_test'          = @()
	'tui_test'                = @()
}

. (Join-Path $PSScriptRoot 'private\qmake_build.ps1')
Initialize-QtKit

$repositoryRoot = Split-Path $PSScriptRoot -Parent
if (-not $NoBuild) { Build-QmakeProject (Join-Path $repositoryRoot 'file-commander-core\core-tests\core-tests.pro') $Configuration }

if ($BuildOnly) { exit 0 }

# The suites run on the offscreen platform: the widget suites would otherwise open windows on the desktop.
# A QT_QPA_PLATFORM already set is kept.
if (-not $env:QT_QPA_PLATFORM) { $env:QT_QPA_PLATFORM = 'offscreen' }

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
