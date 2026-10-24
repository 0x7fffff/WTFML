# Stress the offscreen UI driver and preserve the first failing run.
#
# Not registered in CTest: a full run is several seconds and this is for the
# historical heap-corruption investigation, not the normal regression gate.
#
# The driver intentionally disables QAccessible in the offscreen harness. With
# accessibility active the same binary crashes inside
# InstanceSettingsDialog::~InstanceSettingsDialog() on Windows/Qt 6.10.1 (see
# tools/../tests/ui_check/main.cpp); the stress loop makes that evidence cheap
# to reproduce after a Qt or test-harness change.

[CmdletBinding()]
param(
    [int]$Runs = 50,
    [string]$UiCheck,
    [string]$OutDir,
    [switch]$StopOnFailure
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $UiCheck) {
    $UiCheck = Join-Path $repo 'build/verify/tests/ui_check/ui_check.exe'
}
if (-not (Test-Path -LiteralPath $UiCheck -PathType Leaf)) {
    throw "ui_check.exe not found: $UiCheck (build the ui_check target first)"
}
if ($Runs -lt 1) {
    throw "-Runs must be at least 1"
}
if (-not $OutDir) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutDir = Join-Path $repo "build/ui-stress/$stamp"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_FORCE_STDERR_LOGGING = '1'
$failed = 0
$firstFailure = $null

for ($i = 1; $i -le $Runs; ++$i) {
    $log = Join-Path $OutDir ("run-{0:D3}.log" -f $i)
    # Native stderr (Qt's font-dir warning) must not be treated as a terminating
    # PowerShell error; the process exit code is the judge.
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $UiCheck *> $log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
    if ($code -ne 0) {
        if ($failed -eq 0) {
            $failed = $i
            $firstFailure = $log
        }
        Write-Host ("FAIL run {0}/{1}, exit={2}: {3}" -f $i, $Runs, $code, $log)
        if ($StopOnFailure) {
            break
        }
    } else {
        Write-Host ("PASS run {0}/{1}" -f $i, $Runs)
    }
}

$summary = [ordered]@{
    runs = $Runs
    completed = $(if ($failed -eq 0) { $Runs } else { $failed })
    failed = ($failed -ne 0)
    firstFailure = $firstFailure
    outDir = $OutDir
    qtQpaPlatform = $env:QT_QPA_PLATFORM
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json') -Encoding UTF8

if ($failed -ne 0) {
    Write-Host "ui offscreen stress: FAIL (first failure run $failed)"
    exit 1
}
Write-Host "ui offscreen stress: PASS ($Runs/$Runs) -> $OutDir"
exit 0
