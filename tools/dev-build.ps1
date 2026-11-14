#Requires -Version 7.0
<#
.SYNOPSIS
    增量构建开发用的启动器 exe（build/verification-mingw13），不跑测试。

.DESCRIPTION
    给改完代码想直接看效果的人用：只构建 WTFMinecraftLauncher 一个目标，
    并行度默认 CPU 核数减 2。构建目录不存在时按 CMakePresets.json 的
    verification 预设配置一次，并打开 WTFML_BUILD_NETWORK_DIAG（与 CI 一致）。

    Qt 与 MinGW 的 bin 目录取自已有 CMakeCache.txt（CMAKE_PREFIX_PATH、CMAKE_CXX_COMPILER），
    读不到用本机默认路径。检测到 ctest / mw_check / ui_check 在跑时停止，
    免得和测试抢 CPU 或踩同一个构建目录；确实要编加 -Force。

.PARAMETER Jobs
    并行度，默认 CPU 核数减 2（至少 1）。

.PARAMETER DryRun
    只打印将要执行的命令，不配置、不构建。

.PARAMETER Force
    有测试进程在跑也继续构建。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/verification-mingw13',
    [ValidateRange(0, 64)][int]$Jobs = 0,
    [switch]$DryRun,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$exe = Join-Path $build 'WTFMinecraftLauncher.exe'
$target = 'WTFMinecraftLauncher'
if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 2) }

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { throw '找不到 cmake。把 Qt 自带的 CMake（D:/Qt/Qt/Tools/CMake_64/bin）加进 PATH 再试。' }

# ---- Qt / MinGW 路径：优先进已有的构建缓存，其次用本机默认 ----
$defaultQt = 'D:/Qt/Qt/6.10.1/mingw_64'
$defaultMinGw = 'D:/Qt/Qt/Tools/mingw1310_64/bin'
$cachePath = Join-Path $build 'CMakeCache.txt'
$qtPrefix = ''
$minGwBin = ''
if (Test-Path -LiteralPath $cachePath) {
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([A-Za-z0-9_]+)(?::[A-Z]+)?=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    # 缓存里一般只有 Qt6Core_DIR（.../mingw_64/lib/cmake/Qt6Core），没有 CMAKE_PREFIX_PATH。
    $qtPrefix = if ($cache['CMAKE_PREFIX_PATH']) { ($cache['CMAKE_PREFIX_PATH'] -split ';')[0] }
        elseif ($cache['Qt6Core_DIR']) { [IO.Path]::GetFullPath((Join-Path $cache['Qt6Core_DIR'] '../../..')) }
        else { '' }
    if ($cache['CMAKE_CXX_COMPILER']) { $minGwBin = Split-Path -Parent $cache['CMAKE_CXX_COMPILER'] }
}
if (-not $qtPrefix) { $qtPrefix = $defaultQt }
if (-not $minGwBin) { $minGwBin = $defaultMinGw }
$qtBin = Join-Path $qtPrefix 'bin'

# ---- 测试在跑就别编：抢 CPU，还可能踩同一个 build 目录 ----
if (-not $DryRun -and -not $Force) {
    $busy = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -match '^(ctest|mw_check.*|ui_check.*)$' })
    if ($busy.Count -gt 0) {
        $names = ($busy | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ', '
        throw "有测试进程在跑：$names。等它们结束再编，或者加 -Force 强行构建。"
    }
}

$env:PATH = (@($qtBin, $minGwBin, $env:PATH) -join ';')
$env:QT_PLUGIN_PATH = Join-Path $qtPrefix 'plugins'

$needConfigure = -not (Test-Path -LiteralPath $cachePath)
$configureArgs = @('--preset', 'verification', '-B', $build, '-DWTFML_BUILD_NETWORK_DIAG=ON')
$cmdArgs = @('--build', $build, '--target', $target, '--parallel', $Jobs)
if ($needConfigure) {
    Write-Host "先配置（$build 还没配置过）："
    Write-Host "  cmake $($configureArgs -join ' ')"
}
Write-Host "增量构建（$Jobs 路并行）："
Write-Host "  cmake $($cmdArgs -join ' ')"
Write-Host "  PATH 前置：$qtBin;$minGwBin"
Write-Host "  QT_PLUGIN_PATH=$env:QT_PLUGIN_PATH"

if ($DryRun) { Write-Host '[DryRun] 不执行。' -ForegroundColor Yellow; exit 0 }

$clock = [Diagnostics.Stopwatch]::StartNew()
if ($needConfigure) {
    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "配置失败（退出码 $LASTEXITCODE）。" }
}
& $cmake @cmdArgs
if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）。" }
$clock.Stop()

if (-not (Test-Path -LiteralPath $exe)) { throw "构建报告成功，但找不到 exe：$exe" }
$info = Get-Item -LiteralPath $exe
Write-Host ''
Write-Host ("构建完成，用时 {0:N1} s" -f $clock.Elapsed.TotalSeconds)
Write-Host "exe：$($info.FullName)"
Write-Host ("大小：{0:N1} MB" -f ($info.Length / 1MB))
Write-Host "修改时间：$($info.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
exit 0
