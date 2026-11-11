#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Exe = 'build/verification-mingw13/tests/mw_check/wtfml_desktop_acceptance.exe',
    [Parameter(Mandatory)][string]$Root,
    [switch]$Extended,
    [switch]$LogStress,
    # 设计稿对照截图（主页三主题 / 窄宽窗 / 折叠轨 / 组件总表），程序内抵消系统缩放到 1:1 像素
    [switch]$DesignShots,
    # 设置 / 活动中心 / 任务详情 / 账号 / 游戏日志 / 对话框对照截图
    [switch]$PageShots,
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
if ($DesignShots) { $arguments += '--design-shots' }
if ($PageShots) { $arguments += '--page-shots'; $DesignShots = $true }
$p = Start-Process -FilePath $program -ArgumentList $arguments -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    $p.Kill($true)
    throw "Desktop acceptance exceeded ${TimeoutSeconds}s; owned process stopped; logs retained."
}
if ($p.ExitCode -ne 0) { throw "Desktop acceptance failed ($($p.ExitCode)); see $err" }
if ($DesignShots) {
    $shots = Get-Content -LiteralPath (Join-Path $evidence 'design-shots.json') -Raw | ConvertFrom-Json
    Write-Host "Design shots: $($shots.shots.Count) at device pixel ratio $($shots.devicePixelRatio)"
    Write-Host "Evidence: $evidence"
    return
}
$report = Join-Path $evidence 'report.json'
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if ($LogStress) {
    if (@($result.checks | Where-Object { -not $_.ok }).Count) { throw "Native log acceptance failed; see $report" }
    Write-Host "Native log windows: $($result.rowsPerSession) rows each; heartbeat gap $($result.maxHeartbeatGapMs)ms"
}
if (-not $LogStress) { Write-Host "Windows scale: $([math]::Round($result.devicePixelRatio * 100))%; native screenshots: $($result.samples.Count)" }
Write-Host "Evidence: $report"
