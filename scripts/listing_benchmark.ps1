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

	[string[]]$Variant = @('qt', 'qt-unsorted', 'thinio', 'panel'),

	# Tags the CSV rows, e.g. with the build being measured; the VHDX file name is appended
	[string]$Label = '',

	# Defaults to listing_benchmark_warm.csv or listing_benchmark_cold.csv beside the benchmark executable
	[string]$Csv = '',

	[ValidateSet('release', 'debug')]
	[string]$Configuration = 'release',

	# Passed to the benchmark by -Warm, e.g. --runs 30: a repeated option takes its last value, so these win
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
if (-not $Setup -and -not $Warm -and $Samples -le 0) { Fail 'Nothing to do: pass -Setup, -Warm, -Samples N, or a combination.' }
if ($BenchmarkArguments -and -not $Warm) { Fail "Only -Warm passes arguments on to the benchmark: $($BenchmarkArguments -join ' ')" }

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
$warmCsv = if ($Csv) { $Csv } else { Join-Path (Split-Path $benchmark -Parent) 'listing_benchmark_warm.csv' }
$coldCsv = if ($Csv) { $Csv } else { Join-Path (Split-Path $benchmark -Parent) 'listing_benchmark_cold.csv' }

# powershell -File passes "a,b" as one string, not as an array
# diskpart and the Storage cmdlets both need absolute paths
$VhdxPath = @($VhdxPath -split ',' | ForEach-Object Trim | Where-Object { $_ } | ForEach-Object { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($_) })
$Variant = @($Variant -split ',' | ForEach-Object Trim | Where-Object { $_ })

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

function Get-RowLabel([string]$vhdx)
{
	$leaf = Split-Path $vhdx -Leaf
	if ($Label) { return "$Label/$leaf" }
	return $leaf
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
		$root = Mount-Vhdx $vhdx 'ReadOnly'
		$folders = @(Get-BenchmarkFolderNames $vhdx $root | ForEach-Object { Join-Path $root $_ })

		& $benchmark --variants ($Variant -join ',') --label (Get-RowLabel $vhdx) --csv $warmCsv @BenchmarkArguments @folders
		if ($LASTEXITCODE -ne 0) { Fail "The benchmark failed on $vhdx" }

		Dismount-Vhdx $vhdx
	}

	Write-Host "Results appended to $warmCsv"
}

if ($Samples -gt 0)
{
	# Read once up front: listing a volume root between the samples would warm it
	$jobs = @()
	foreach ($vhdx in $VhdxPath)
	{
		$root = Mount-Vhdx $vhdx 'ReadOnly'
		$folders = @(Get-BenchmarkFolderNames $vhdx $root)
		Dismount-Vhdx $vhdx

		foreach ($folder in $folders)
		{
			foreach ($name in $Variant) { $jobs += [pscustomobject]@{ Vhdx = $vhdx; Folder = $folder; Variant = $name } }
		}
	}

	for ($sample = 1; $sample -le $Samples; ++$sample)
	{
		Write-Host "Sample $sample of $Samples"
		# Each round is shuffled: the drive's own cache survives the remount, and must not favour the same jobs every time
		foreach ($job in ($jobs | Get-Random -Count $jobs.Count))
		{
			# Dismounting discards everything cached for the volume; read-only, so that the listing writes nothing back
			Dismount-Vhdx $job.Vhdx
			$root = Mount-Vhdx $job.Vhdx 'ReadOnly'

			& $benchmark --once --variants $job.Variant --label (Get-RowLabel $job.Vhdx) --csv $coldCsv (Join-Path $root $job.Folder)
			if ($LASTEXITCODE -ne 0) { Fail "The benchmark failed on $($job.Folder) in $($job.Vhdx)" }
		}
	}

	foreach ($vhdx in $VhdxPath) { Dismount-Vhdx $vhdx }
	Write-Host "Results appended to $coldCsv"
}
