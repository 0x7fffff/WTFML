#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('quick', 'full', 'core', 'window', 'java', 'mods', 'upgrade', 'network', 'visual')]
    [string]$Suite = 'quick',
    [string]$BuildDir = 'build/verification-mingw13',
    [string]$ConfigurePreset = 'verification',
    [string]$ReportRoot = 'build/test-runs',
    [switch]$NoBuild,
    [switch]$ListOnly,
    [ValidateRange(1, 900)][int]$BuildTimeoutSeconds = 600,
    [ValidateRange(1, 900)][int]$TestTimeoutSeconds = 900,
    [ValidateRange(1, 16)][int]$BuildJobs = 2
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$reports = [IO.Path]::GetFullPath($ReportRoot, $repo)
$run = Join-Path $reports ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$null = New-Item -ItemType Directory -Path $run -Force
$null = New-Item -ItemType Directory -Path (Join-Path $run 'temp') -Force
$stages = [Collections.Generic.List[object]]::new()
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$hasher = [Security.Cryptography.SHA256]::Create()
try { $lockKey = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($build.ToLowerInvariant()))).Replace('-', '') }
finally { $hasher.Dispose() }
$mutex = [Threading.Mutex]::new($false, "Local\WTFML.Tests.$lockKey")
$ownsMutex = $false
$result = 1
$failure = $null

function Invoke-Stage([string]$Name, [string]$Program, [string[]]$Arguments, [int]$Timeout) {
    $out = Join-Path $run "$Name.stdout.log"
    $err = Join-Path $run "$Name.stderr.log"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "[$Name] starting; limit ${Timeout}s; logs: $run"
    # Start-Process joins ArgumentList on Windows; quote every argument, including paths with spaces.
    $quoted = @($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $process = Start-Process -FilePath $Program -ArgumentList $quoted -WorkingDirectory $repo -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    $timedOut = $false
    try {
        $nextNotice = 30
        while (-not $process.WaitForExit(1000)) {
            if ($watch.Elapsed.TotalSeconds -ge $Timeout) {
                $timedOut = $true
                $process.Kill($true)
                $null = $process.WaitForExit(10000)
                break
            }
            if ($watch.Elapsed.TotalSeconds -ge $nextNotice) {
                Write-Host "[$Name] still running ($([int]$watch.Elapsed.TotalSeconds)s); latest output:"
                Get-Content -LiteralPath $out -Tail 3 -ErrorAction SilentlyContinue | Write-Host
                $nextNotice += 30
            }
        }
        $process.Refresh()
        $code = if ($timedOut) { 124 } else { $process.ExitCode }
        $stages.Add([pscustomobject]@{ name = $Name; exitCode = $code; timedOut = $timedOut; seconds = [math]::Round($watch.Elapsed.TotalSeconds, 2); arguments = $Arguments })
        Get-Content -LiteralPath $out -Tail 18 -ErrorAction SilentlyContinue | Write-Host
        Get-Content -LiteralPath $err -Tail 18 -ErrorAction SilentlyContinue | Write-Host
        if ($code -ne 0) { throw "Stage '$Name' failed (exit $code). First failure logs preserved: $run" }
    } finally {
        if (-not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

try {
    try { $ownsMutex = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw "Another verification run owns this build directory: $build" }
    $env:TEMP = Join-Path $run 'temp'
    $env:TMP = $env:TEMP
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $ctest = (Get-Command ctest -ErrorAction Stop).Source
    if (-not $NoBuild -and -not $ListOnly) {
        Get-Process WTFMinecraftLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
        if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
            Invoke-Stage 'configure' $cmake @('--preset', $ConfigurePreset, '-B', $build) $BuildTimeoutSeconds
        }
        $targets = switch ($Suite) {
            'core' { @('verify') }
            'java' { @('mw_check') }
            'window' { @('mw_check') }
            'mods' { @('mw_check') }
            'upgrade' { @('WTFMinecraftLauncher', 'mw_check') }
            'network' { @('mw_check') }
            'visual' { @('ui_check') }
            'quick' { @('ui_check', 'mw_check') }
            default { @('WTFMinecraftLauncher', 'verify', 'ui_check', 'mw_check') }
        }
        foreach ($target in $targets) {
            Invoke-Stage "build-$target" $cmake @('--build', $build, '--target', $target, '--parallel', "$BuildJobs") $BuildTimeoutSeconds
        }
        if ($Suite -eq 'full') {
            # Build optional/default targets too (e.g. network diagnostics and future registered tools).
            Invoke-Stage 'build-remaining' $cmake @('--build', $build, '--parallel', "$BuildJobs") $BuildTimeoutSeconds
        }
    }
    $selection = switch ($Suite) {
        'quick' { @('-R', '^wtfml\.(test-runner|java-manage|mod-action-panel|workspace|activity-selection|theme|ui-refresh|install-layout|lifecycle|toolbox-polish|armor-trim)$') }
        'core' { @('-R', '^wtfml\.core$') }
        'java' { @('-R', '^wtfml\.java-manage$') }
        'window' { @('-R', '^wtfml\.main-window$') }
        'mods' { @('-R', '^wtfml\.mod-action-panel$') }
        'upgrade' { @('-R', '^wtfml\.instance-upgrade$') }
        'network' { @('-R', '^wtfml\.(download-retry|high-speed-download|segmented-download|segment-install|preset-install)$') }
        'visual' { @('-L', 'visual') }
        'full' { @('-LE', 'visual') }
    }
    $testArgs = @('--test-dir', $build, '--no-tests=error', '--output-on-failure', '--parallel', '1', '--timeout', '900') + $selection
    if ($ListOnly) { $testArgs += @('--show-only=json-v1') }
    else { $testArgs += @('--verbose', '--output-junit', (Join-Path $run 'results.xml')) }
    Invoke-Stage 'test' $ctest $testArgs $TestTimeoutSeconds
    $result = 0
} catch {
    $failure = "$_"
    Write-Host "ERROR: $_" -ForegroundColor Red
} finally {
    $summary = [ordered]@{ suite = $Suite; buildDir = $build; listOnly = [bool]$ListOnly; noBuild = [bool]$NoBuild; exitCode = $result; error = $failure; stages = @($stages.ToArray()) }
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    Write-Host "Verification report: $run"
}
exit $result
