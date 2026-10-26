#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/verification-mingw13',
    [ValidateRange(1, 16)][int]$Jobs = 6,
    [ValidateRange(1, 900)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) { throw 'An existing configured verification build is required.' }
$report = Join-Path $repo ('build/perf-test-links-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$null = New-Item -ItemType Directory -Path $report
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$hasher = [Security.Cryptography.SHA256]::Create()
try { $key = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($build.ToLowerInvariant()))).Replace('-', '') }
finally { $hasher.Dispose() }
$mutex = [Threading.Mutex]::new($false, "Local\WTFML.Tests.$key")
$owned = $false
$rows = [Collections.Generic.List[object]]::new()
$archive = Join-Path $build 'tests/libwtfml_core_runtime.a'
$archiveTime = $null
$completed = $false

function Run([string]$Name, [string[]]$Arguments) {
    $out = Join-Path $report "$Name.stdout.log"
    $err = Join-Path $report "$Name.stderr.log"
    $quoted = @($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "[$Name] limit ${TimeoutSeconds}s; $report"
    $process = Start-Process -FilePath $cmake -ArgumentList $quoted -WorkingDirectory $repo -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    try {
        $notice = 30
        while (-not $process.WaitForExit(1000)) {
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $process.Kill($true); $null = $process.WaitForExit(10000)
                throw "$Name timed out; logs retained at $report"
            }
            if ($watch.Elapsed.TotalSeconds -ge $notice) {
                Get-Content -LiteralPath $out -Tail 2 | Write-Host
                $notice += 30
            }
        }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "$Name failed ($($process.ExitCode)); logs retained at $report" }
        $text = Get-Content -LiteralPath $out -Raw
        [pscustomobject]@{name=$Name; seconds=[math]::Round($watch.Elapsed.TotalSeconds, 3);
            compiled=([regex]::Matches($text, 'Building CXX object')).Count;
            linked=([regex]::Matches($text, 'Linking CXX executable')).Count}
    } finally {
        if (-not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

try {
    try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { throw 'Another run owns this verification build.' }
    foreach ($mode in @('Standalone', 'Compact')) {
        $enabled = if ($mode -eq 'Compact') { 'ON' } else { 'OFF' }
        $null = Run "$mode-configure" @('-S', $repo, '-B', $build, "-DWTFML_COMPACT_TEST_RUNNERS=$enabled")
        $targets = @(Get-Content -LiteralPath (Join-Path $build 'test-targets.txt') |
            ConvertFrom-Csv -Delimiter '|' | Where-Object target -Match '^(mw_check_|ui_check_)' |
            Select-Object -ExpandProperty target -Unique | Sort-Object)
        if (-not $targets.Count) { throw 'No specialist test owners found.' }
        # Walk the shared dependency graph once in both modes. Building each old
        # target separately would measure repeated Make traversals as well as links.
        $buildArguments = @('--build', $build, '--parallel', "$Jobs", '--target', 'wtfml_specialist_modules')
        $null = Run "$mode-warm" $buildArguments
        if (-not (Test-Path -LiteralPath $archive)) { throw "Shared archive missing: $archive" }
        if ($null -eq $archiveTime) { $archiveTime = (Get-Item -LiteralPath $archive).LastWriteTimeUtc }
        # Content remains unchanged; only force dependent executable links.
        # MinGW Make can compare Windows timestamps at second resolution; avoid
        # touching in the same tick as the last warm link (a false no-op).
        Start-Sleep -Milliseconds 1200
        (Get-Item -LiteralPath $archive).LastWriteTimeUtc = [DateTime]::UtcNow
        $row = Run "$mode-relink" $buildArguments
        $row | Add-Member -NotePropertyName targets -NotePropertyValue $targets.Count
        $rows.Add($row)
        if ($row.compiled -ne 0 -or $row.linked -ne $targets.Count) {
            throw 'Measurement was not pure relinking; inspect retained logs instead of claiming a speedup.'
        }
        Write-Host "$mode : $($row.seconds)s, $($row.linked) executable links"
    }
    $completed = $true
} finally {
    if ($null -ne $archiveTime -and (Test-Path -LiteralPath $archive)) { (Get-Item -LiteralPath $archive).LastWriteTimeUtc = $archiveTime }
    @{ok=$completed; build=$build; jobs=$Jobs; measuredAt=(Get-Date -Format o); steps=@($rows.ToArray());
      scope='Warm specialist UI/integration relinking only; not a cold build or test-runtime comparison.'} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $report 'summary.json') -Encoding utf8
    if ($owned) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    Write-Host "Evidence: $report"
}
