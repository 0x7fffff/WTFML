#Requires -Version 7.0
<#
.SYNOPSIS
    玩家情景压力脚本：并发下载 + 有界磁盘占用。

.DESCRIPTION
    这一份只做两件"真网络、真产品代码、但可控"的事：

    1. 并发跑 N 个 `mw_check resource-live`（Modrinth 小文件：模组/材质包/光影/数据包轮换），
       每个进程一个独立 TEMP；记录成功率、总耗时、每个的耗时；
    2. 统计这一趟在构建目录里留下的沙盒字节数（`mw-check-*`），打印后**删掉本趟新建的**，
       保证磁盘不被写爆（日志留在 Root 下，产物不留）。

    所有数据写在 `-Root`（默认 `D:\Desktop\the_project\.tmp-test\player-stress-<时间戳>`），
    不碰 C 盘用户目录，也不碰真实的 .minecraft。

.PARAMETER BuildDir
    构建目录（含 tests/mw_check/mw_check.exe）。默认 `D:\Desktop\the_project\build-audit`。

.PARAMETER Jobs
    并发进程数，默认 10。

.PARAMETER Root
    本次运行的证据目录；默认在项目 `.tmp-test` 下按时间戳新建。

.PARAMETER TimeoutSeconds
    每个进程的上限，默认 180。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'D:\Desktop\the_project\build-audit',
    [int]$Jobs = 10,
    [string]$Root = '',
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BuildDir 'tests\mw_check\mw_check.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "没有这个探针：$exe" }

$workDir = Join-Path $BuildDir 'tests\mw_check'
$before = @(Get-ChildItem -LiteralPath $workDir -Directory -Filter 'mw-check-*' -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName)

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Join-Path 'D:\Desktop\the_project\.tmp-test' ('player-stress-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $Root | Out-Null
$logDir = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# 轮换四类资源，顺便覆盖"落点目录各不相同"那一条。
$kinds = @(
    @{ kind = 'mod';          term = 'sodium' },
    @{ kind = 'mod';          term = 'lithium' },
    @{ kind = 'resourcepack'; term = 'fresh' },
    @{ kind = 'shader';       term = 'complementary' },
    @{ kind = 'datapack';     term = 'terralith' }
)

$runs = @()
for ($i = 0; $i -lt $Jobs; $i++) {
    $spec = $kinds[$i % $kinds.Count]
    $temp = Join-Path $Root ("temp-$i")
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    $out = Join-Path $logDir ("run-$i.out.log")
    $err = Join-Path $logDir ("run-$i.err.log")
    $env:TEMP = $temp
    $env:TMP = $temp
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = "$BuildDir;$workDir;D:\Qt\Qt\6.10.1\mingw_64\bin;$env:PATH"
    $proc = Start-Process -FilePath $exe `
        -ArgumentList @('resource-live', $spec.kind, $spec.term, 'modrinth') `
        -WorkingDirectory $BuildDir -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $out -RedirectStandardError $err
    $runs += [pscustomobject]@{
        Index = $i; Kind = $spec.kind; Term = $spec.term
        Process = $proc; Output = $out; Error = $err; Started = (Get-Date)
    }
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
foreach ($run in $runs) {
    $remaining = [int][Math]::Max(1, ($deadline - (Get-Date)).TotalMilliseconds)
    $ok = $run.Process.WaitForExit($remaining)
    if (-not $ok) {
        try { $run.Process.Kill($true) } catch { }
    }
    $run | Add-Member -NotePropertyName TimedOut -NotePropertyValue (-not $ok)
    $run | Add-Member -NotePropertyName Seconds -NotePropertyValue ([Math]::Round(((Get-Date) - $run.Started).TotalSeconds, 1))
    $run | Add-Member -NotePropertyName ExitCode -NotePropertyValue $(if ($ok) { $run.Process.ExitCode } else { -1 })
    $text = if (Test-Path $run.Output) { Get-Content -LiteralPath $run.Output -Raw } else { '' }
    $run | Add-Member -NotePropertyName Passed -NotePropertyValue ($text -match '全部通过')
}

$runs | Select-Object Index, Kind, Term, ExitCode, Seconds, Passed, TimedOut |
    Format-Table -AutoSize | Out-String | Write-Host

$passed = @($runs | Where-Object { $_.Passed }).Count
$failed = $runs.Count - $passed

# 本趟新建的沙盒目录：统计并清理（只删本趟的，不碰旧的）。
$after = @(Get-ChildItem -LiteralPath $workDir -Directory -Filter 'mw-check-*' -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty FullName)
$newDirs = @($after | Where-Object { $before -notcontains $_ })
$bytes = 0
foreach ($dir in $newDirs) {
    $bytes += [int64](Get-ChildItem -LiteralPath $dir -Recurse -File -ErrorAction SilentlyContinue |
        Measure-Object Length -Sum).Sum
}
foreach ($dir in $newDirs) {
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
}

[pscustomobject]@{
    Root = $Root
    Jobs = $Jobs
    Passed = $passed
    Failed = $failed
    TotalSeconds = [Math]::Round(($runs | Measure-Object Seconds -Sum).Sum, 1)
    SandboxDirsRemoved = $newDirs.Count
    SandboxMegabytes = [Math]::Round($bytes / 1MB, 1)
} | Format-List | Out-String | Write-Host

if ($failed -gt 0) { exit 1 }
exit 0

