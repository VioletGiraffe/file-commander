# Runs listing_benchmark on dedicated VHDX volumes: warm, and cold samples that remount the disk to discard its cache.
# Needs an elevated prompt: creating, mounting and dismounting a virtual disk is administrative.
# What to verify before trusting the numbers: doc/testing.md, "Listing benchmark".

[CmdletBinding(PositionalBinding = $false)]
param(
	# One VHDX per disk to measure, e.g. one on an SSD and one on an HDD
	[Parameter(Mandatory)]
	[string[]]$VhdxPath,

	# Creates each missing VHDX and generates the benchmark folders on it
	[switch]$Setup,

	# Times every folder of a disk with a warm cache, in one run of the benchmark
	[switch]$Warm,

	# Cold samples per disk, folder and variant
	[int]$Samples = 0,

	# Remounts the one VHDX read-only and prints its root: the benchmark runs this before every cold listing
	[switch]$Remount,

	[ValidateSet('release', 'debug')]
	[string]$Configuration = 'release',

	# Passed on to the benchmark, e.g. --variants qt,panel or --runs 30
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$BenchmarkArguments = @()
)

$ErrorActionPreference = 'Stop'

function Fail([string]$message)
{
	[Console]::Error.WriteLine($message)
	exit 2
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Fail 'Run this from an elevated prompt.' }
if (-not $Remount -and -not $Setup -and -not $Warm -and $Samples -le 0) { Fail 'Nothing to do: pass -Setup, -Warm, -Samples N, or a combination.' }

# powershell -File passes "a,b" as one string, not as an array
# diskpart and the Storage cmdlets both need absolute paths
$VhdxPath = @($VhdxPath -split ',' | ForEach-Object Trim | Where-Object { $_ } | ForEach-Object { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($_) })

# The letter can change from one mount to the next, and appears shortly after the mount returns
function Get-VolumeRoot([string]$vhdx)
{
	for ($attempt = 0; $attempt -lt 50; ++$attempt)
	{
		$partition = Get-DiskImage -ImagePath $vhdx | Get-Disk | Get-Partition | Where-Object { [int][char]$_.DriveLetter -ne 0 } | Select-Object -First 1
		if ($partition) { return "$($partition.DriveLetter):\" }
		Start-Sleep -Milliseconds 100
	}
	Fail "$vhdx has no volume with a drive letter"
}

function Mount-Vhdx([string]$vhdx, [string]$access)
{
	if (-not (Get-DiskImage -ImagePath $vhdx).Attached) { Mount-DiskImage -ImagePath $vhdx -Access $access | Out-Null }
	return Get-VolumeRoot $vhdx
}

function Dismount-Vhdx([string]$vhdx)
{
	if ((Get-DiskImage -ImagePath $vhdx).Attached) { Dismount-DiskImage -ImagePath $vhdx | Out-Null }
}

# By name, not by path: a remount can assign a different drive letter
function Get-BenchmarkFolderNames([string]$vhdx, [string]$root)
{
	$names = @(Get-ChildItem -Path $root -Directory -Filter 'entries-*' | ForEach-Object Name)
	if ($names.Count -eq 0) { Fail "$vhdx has no benchmark folders: -Setup generates them." }
	return $names
}

function New-BenchmarkVhdx([string]$vhdx)
{
	New-Item -ItemType Directory -Force -Path (Split-Path $vhdx -Parent) | Out-Null
	$diskpartScript = [IO.Path]::GetTempFileName()
	try
	{
		# Fixed size: an expanding file grows wherever the host has room, and seeks within it would be measured too
		Set-Content -Path $diskpartScript -Encoding Ascii -Value @(
			"create vdisk file=`"$vhdx`" maximum=512 type=fixed"
			"select vdisk file=`"$vhdx`""
			'attach vdisk'
			'create partition primary'
			'format fs=ntfs quick label=ListingBench'
			'assign'
		)
		$output = diskpart /s $diskpartScript
		if ($LASTEXITCODE -ne 0) { Fail "diskpart failed to create $vhdx`n$($output -join "`n")" }
	}
	finally { Remove-Item $diskpartScript }
}

if ($Remount)
{
	if ($VhdxPath.Count -ne 1) { Fail '-Remount takes one VHDX.' }

	# Dismounting discards everything cached for the volume; read-only, so that the listing writes nothing back
	Dismount-Vhdx $VhdxPath[0]
	Mount-Vhdx $VhdxPath[0] 'ReadOnly'
	exit 0
}

$repositoryRoot = Split-Path $PSScriptRoot -Parent

# The benchmark needs the Qt DLLs; the kit is found the way run_tests.ps1 finds it
if (-not $env:QT_ROOT_DIR)
{
	$localEnvironment = Join-Path $PSScriptRoot 'local-env.ps1'
	if (Test-Path $localEnvironment) { . $localEnvironment }
}
if (-not $env:QT_ROOT_DIR) { Fail 'QT_ROOT_DIR is not set: set it to the Qt kit directory, or set it in a git-ignored local-env.ps1 beside this script.' }
$env:PATH = (Join-Path $env:QT_ROOT_DIR 'bin') + ';' + $env:PATH

$benchmark = Join-Path $repositoryRoot "bin\$Configuration\listing_benchmark.exe"
if (-not (Test-Path $benchmark)) { Fail "$benchmark is not built: run_tests.bat -BuildOnly builds it." }

if ($Setup)
{
	foreach ($vhdx in $VhdxPath)
	{
		if (-not (Test-Path $vhdx)) { New-BenchmarkVhdx $vhdx }

		$root = Mount-Vhdx $vhdx 'ReadWrite'
		# Windows Search reading the folders after a mount would warm the cache before the sample
		$volume = Get-CimInstance Win32_Volume -Filter "DriveLetter='$($root.Substring(0, 2))'"
		Set-CimInstance -InputObject $volume -Property @{ IndexingEnabled = $false }

		# Folders that already exist are kept, so an interrupted setup can simply be rerun
		& $benchmark --generate $root
		if ($LASTEXITCODE -ne 0) { Fail "Generating the benchmark folders on $vhdx failed" }

		Dismount-Vhdx $vhdx
	}
}

if ($Warm -or $Samples -gt 0)
{
	foreach ($vhdx in $VhdxPath)
	{
		if (-not (Test-Path $vhdx)) { Fail "$vhdx does not exist: -Setup creates it." }
	}
}

if ($Warm)
{
	foreach ($vhdx in $VhdxPath)
	{
		Write-Host "Warm: $vhdx"
		$root = Mount-Vhdx $vhdx 'ReadOnly'
		$folders = @(Get-BenchmarkFolderNames $vhdx $root | ForEach-Object { Join-Path $root $_ })

		& $benchmark @BenchmarkArguments @folders
		if ($LASTEXITCODE -ne 0) { Fail "The benchmark failed on $vhdx" }

		Dismount-Vhdx $vhdx
	}
}

if ($Samples -gt 0)
{
	# The same PowerShell runs the remounts
	$shell = (Get-Process -Id $PID).Path
	$remountCommand = @($shell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Remount', '-VhdxPath')

	foreach ($vhdx in $VhdxPath)
	{
		Write-Host "Cold: $vhdx"
		$root = Mount-Vhdx $vhdx 'ReadOnly'
		$folders = @(Get-BenchmarkFolderNames $vhdx $root)

		# One --remount per argument: Windows PowerShell mangles the embedded quotes a single command line would need
		$remountArguments = @($remountCommand + $vhdx | ForEach-Object { "--remount=$_" })
		& $benchmark --cold $Samples @remountArguments @BenchmarkArguments @folders
		if ($LASTEXITCODE -ne 0) { Fail "The benchmark failed on $vhdx" }

		Dismount-Vhdx $vhdx
	}
}
