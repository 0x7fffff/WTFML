#Requires -Version 7.0
#Requires -RunAsAdministrator
[CmdletBinding()]
param([ValidateSet('Create', 'Status', 'Detach')][string]$Action = 'Status')

$ErrorActionPreference = 'Stop'
# Deliberately no user-supplied disk number, image path, size or filesystem.
$repo = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$root = Join-Path $repo 'build/p0-full-volume'
$image = Join-Path $root 'WTFML-P0-256MB.vhd'
$state = Join-Path $root 'mounted.json'
$label = 'WTFML_P0_ONLY'
$identity = 'WTFML-P0-ISOLATED-VHD-1'
foreach ($part in @($repo, (Join-Path $repo 'build'), $root, $image)) {
    if ((Test-Path -LiteralPath $part) -and
        ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing reparse-point path: $part"
    }
}

function Get-VerifiedDisk {
    $mounted = Get-DiskImage -ImagePath $image -ErrorAction Stop
    if (-not $mounted.Attached -or $mounted.ImagePath -ine $image) { throw 'Expected VHD is not attached.' }
    $disks = @($mounted | Get-Disk)
    if ($disks.Count -ne 1) { throw 'VHD must map to exactly one disk.' }
    $disk = $disks[0]
    if ($disk.IsBoot -or $disk.IsSystem -or $disk.Size -ne 256MB -or
        [string]$disk.BusType -notmatch '^File\s*Backed\s*Virtual$') {
        throw "Disk identity guard failed; nothing will be formatted (disk $($disk.Number))."
    }
    return $disk
}

if ($Action -ne 'Create') {
    if (-not (Test-Path -LiteralPath $state)) { throw 'No test-disk ownership record; refusing operation.' }
    $record = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    if ($record.identity -ne $identity -or $record.image -ine $image) { throw 'Invalid ownership record.' }
    $disk = Get-VerifiedDisk
    if ($disk.UniqueId -ne $record.diskUniqueId) { throw 'Disk identity changed; refusing operation.' }
    if ($Action -eq 'Detach') {
        # Never delete the image or test results. Close processes using this volume first.
        Dismount-DiskImage -ImagePath $image -ErrorAction Stop
        Write-Host "Detached. VHD and all evidence retained at: $root"
    } else {
        $disk | Get-Partition | Get-Volume | Format-Table DriveLetter, FileSystemLabel, SizeRemaining, Size
    }
    exit
}

if (Test-Path -LiteralPath $root) { throw "Test directory already exists; refusing overwrite: $root" }
if ((Read-Host 'Only a NEW 256 MiB test VHD will be formatted. Type WTFML-P0-VHD to proceed') -cne 'WTFML-P0-VHD') {
    throw 'Cancelled; no disk created.'
}
$null = New-Item -ItemType Directory -Path $root
$diskpartScript = Join-Path $root 'create-vhd.txt'
@("create vdisk file=`"$image`" maximum=256 type=expandable",
  "select vdisk file=`"$image`"", 'attach vdisk', 'exit') |
    Set-Content -LiteralPath $diskpartScript -Encoding ascii
$process = Start-Process -FilePath "$env:SystemRoot/System32/diskpart.exe" -ArgumentList @('/s', "`"$diskpartScript`"") `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'diskpart.stdout.log') `
    -RedirectStandardError (Join-Path $root 'diskpart.stderr.log')
try {
    if (-not $process.WaitForExit(30000)) { $process.Kill($true); throw 'DiskPart exceeded 30 seconds; inspect retained logs.' }
    if ($process.ExitCode -ne 0) { throw 'DiskPart failed; inspect retained logs.' }
} finally { $process.Dispose() }
$disk = Get-VerifiedDisk
if ($disk.PartitionStyle -ne 'RAW' -or @($disk | Get-Partition -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'New VHD is not blank; refusing initialization/format.'
}
# Save ownership before initialization, so a later error still permits explicit detach.
@{ identity = $identity; image = $image; diskUniqueId = $disk.UniqueId; createdUtc = [DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
$null = $disk | Initialize-Disk -PartitionStyle MBR -PassThru
$disk = Get-VerifiedDisk
@{ identity = $identity; image = $image; diskUniqueId = $disk.UniqueId } |
    ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
$partition = $disk | New-Partition -UseMaximumSize -AssignDriveLetter
if ($partition.DiskNumber -ne $disk.Number) { throw 'Partition disk mismatch; refusing format.' }
$volume = $partition | Format-Volume -FileSystem NTFS -NewFileSystemLabel $label -Confirm:$false -Force
if ($volume.FileSystemLabel -ne $label -or -not $volume.DriveLetter) { throw 'Unexpected test volume.' }
$volumeRoot = "$($volume.DriveLetter):/"
@{ identity = $identity; image = $image; diskUniqueId = $disk.UniqueId; volumeRoot = $volumeRoot; label = $label } |
    ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
Set-Content -LiteralPath (Join-Path $volumeRoot '.wtfml-p0-volume') -Value $identity -Encoding ascii
Write-Host "Ready: $volumeRoot ($label). This is an empty test volume; no existing volume was formatted."
Write-Host 'Keep it attached and return to the chat. Do not manually fill your real disks.'
Write-Host "Detach later: pwsh -NoProfile -File `"$PSCommandPath`" -Action Detach"
