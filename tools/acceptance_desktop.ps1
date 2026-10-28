#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Exe = 'build/verification-mingw13/tests/mw_check/wtfml_desktop_acceptance.exe',
    [Parameter(Mandatory)][string]$Root,
    [switch]$Extended,
    [switch]$LogStress,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$program = [IO.Path]::GetFullPath($Exe, $repo)
$evidence = [IO.Path]::GetFullPath($Root, $repo)
if (-not (Test-Path -LiteralPath $program -PathType Leaf)) { throw "Build the desktop acceptance target first: $program" }
if (Test-Path -LiteralPath $evidence) { throw "Refusing existing evidence directory: $evidence" }
foreach ($key in @('QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'QT_FONT_DPI', 'QT_QPA_PLATFORM')) {
    if ([Environment]::GetEnvironmentVariable($key)) { throw "Remove $key before testing real Windows DPI." }
}
# This is an intentionally visible test window; it closes itself after sampling.
$log = $evidence + '.stdout.log'
$err = $evidence + '.stderr.log'
$null = New-Item -ItemType Directory -Force -Path (Split-Path $evidence -Parent)
$arguments = @('--root', ('"' + $evidence + '"'))
if ($Extended) { $arguments += '--extended' }
if ($LogStress) { $arguments += '--log-stress' }
$p = Start-Process -FilePath $program -ArgumentList $arguments -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    $p.Kill($true)
    throw "Desktop acceptance exceeded ${TimeoutSeconds}s; owned process stopped; logs retained."
}
if ($p.ExitCode -ne 0) { throw "Desktop acceptance failed ($($p.ExitCode)); see $err" }
$report = Join-Path $evidence 'report.json'
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if ($LogStress) {
    if (@($result.checks | Where-Object { -not $_.ok }).Count) { throw "Native log acceptance failed; see $report" }
    Write-Host "Native log windows: $($result.rowsPerSession) rows each; heartbeat gap $($result.maxHeartbeatGapMs)ms"
}
if (-not $LogStress) { Write-Host "Windows scale: $([math]::Round($result.devicePixelRatio * 100))%; native screenshots: $($result.samples.Count)" }
Write-Host "Evidence: $report"
