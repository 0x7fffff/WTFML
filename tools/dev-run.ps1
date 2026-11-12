#Requires -Version 7.0
<#
.SYNOPSIS
    用动态 Qt 的开发构建启动启动器，不阻塞终端。

.DESCRIPTION
    exe 取 build/verification-mingw13/WTFMinecraftLauncher.exe。给子进程的 PATH
    前置 Qt 的 bin 与 MinGW 13.1 的 bin，并设 QT_PLUGIN_PATH 指向 Qt 的 plugins，
    否则动态链接的 exe 起不来。

    两个目录从已有 CMakeCache.txt 读（CMAKE_PREFIX_PATH 或 Qt6Core_DIR、
    CMAKE_CXX_COMPILER），读不到用本机默认路径。

.PARAMETER DryRun
    只打印将要执行的命令，不启动程序。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/verification-mingw13',
    [string]$QtPrefix = '',
    [string]$MinGwBin = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$exe = Join-Path $build 'WTFMinecraftLauncher.exe'

if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 exe：$exe`n先跑 ./tools/dev-build.ps1 编出来，再回来运行。"
}

# ---- Qt / MinGW 路径：优先读构建缓存，缓存里一般只有 Qt6Core_DIR ----
$defaultQt = 'D:/Qt/Qt/6.10.1/mingw_64'
$defaultMinGw = 'D:/Qt/Qt/Tools/mingw1310_64/bin'
$cachePath = Join-Path $build 'CMakeCache.txt'
if (-not $QtPrefix -or -not $MinGwBin) {
    $cache = @{}
    if (Test-Path -LiteralPath $cachePath) {
        foreach ($line in Get-Content -LiteralPath $cachePath) {
            if ($line -match '^([A-Za-z0-9_]+)(?::[A-Z]+)?=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
        }
    }
    if (-not $QtPrefix) {
        $QtPrefix = if ($cache['CMAKE_PREFIX_PATH']) { ($cache['CMAKE_PREFIX_PATH'] -split ';')[0] }
            elseif ($cache['Qt6Core_DIR']) { [IO.Path]::GetFullPath((Join-Path $cache['Qt6Core_DIR'] '../../..')) }
            else { $defaultQt }
    }
    if (-not $MinGwBin) {
        $MinGwBin = if ($cache['CMAKE_CXX_COMPILER']) { Split-Path -Parent $cache['CMAKE_CXX_COMPILER'] } else { $defaultMinGw }
    }
}
$qtBin = Join-Path $QtPrefix 'bin'

$env:PATH = (@($qtBin, $MinGwBin, $env:PATH) -join ';')
$env:QT_PLUGIN_PATH = Join-Path $QtPrefix 'plugins'

Write-Host "exe：$exe"
Write-Host "PATH 前置：$qtBin;$MinGwBin"
Write-Host "QT_PLUGIN_PATH=$env:QT_PLUGIN_PATH"
if ($DryRun) {
    Write-Host "[DryRun] Start-Process -FilePath '$exe' -WorkingDirectory '$repo'  （不执行）" -ForegroundColor Yellow
    exit 0
}

$process = Start-Process -FilePath $exe -WorkingDirectory $repo -PassThru
Write-Host "已启动，PID $($process.Id)"
exit 0
