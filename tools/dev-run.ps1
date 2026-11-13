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

.PARAMETER AcceptanceProfile
    起一份临时设置沙盒（`--acceptance-profile`）：设置与数据都落在临时目录，跑完可以直接删掉，
    不碰本机 `HKCU\Software\WTFML\WTFML`。自动化、走查、点遍控件的活一律用它。

.PARAMETER CloseAfterSeconds
    配合 `-AcceptanceProfile`：多少秒后自动关窗（2~120，默认 120，留给脚本截图/点控件）。

.PARAMETER DryRun
    只打印将要执行的命令，不启动程序。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/verification-mingw13',
    [string]$QtPrefix = '',
    [string]$MinGwBin = '',
    [switch]$AcceptanceProfile,
    [int]$CloseAfterSeconds = 0,
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

# ---- 设置/data 落在哪儿 ----
# 默认这一条是**真**设置（Windows 上 `HKCU\Software\WTFML\WTFML`）：这是"人拿它用一下"的那条路
# （用户自己的主题、账号、上次打开日期都在里面）。自动化 / 走查脚本**不许**走这条：点在设置页的
# 滑块上会把 0 直接写进用户设置（2026-11-13 的 BackdropTint=0 / BackdropBlur=0 就是这么来的）。
# 那种跑法加 -AcceptanceProfile：临时目录里一套干净设置，跑完原样删掉，不碰真 HKCU。
$profileArgs = @()
if ($AcceptanceProfile) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $profile = Join-Path ([IO.Path]::GetTempPath()) "wtfml-dev-run-$stamp"
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    # 和 src/util/StartupAcceptance.hpp 的配额对齐：closeAfterMs 2s~120s。
    $closeAfterMs = if ($CloseAfterSeconds -gt 0) { [Math]::Min([Math]::Max($CloseAfterSeconds * 1000, 2000), 120000) } else { 120000 }
    @{format = 'wtfml-startup-acceptance/1'; closeAfterMs = $closeAfterMs; stressRows = 0} |
        ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $profile 'acceptance-profile.json') -Encoding utf8 -NoNewline
    $profileArgs = @('--acceptance-profile', "`"$profile`"")
    Write-Host "设置沙盒：$profile（--acceptance-profile；跑完可以整个删掉）" -ForegroundColor Green
} else {
    Write-Host "注意：这一趟读写的是**你本机的真实设置**（HKCU\Software\WTFML\WTFML）。自动化请加 -AcceptanceProfile。" -ForegroundColor Yellow
}

if ($DryRun) {
    Write-Host "[DryRun] Start-Process -FilePath '$exe' -ArgumentList '$($profileArgs -join ' ')' -WorkingDirectory '$repo'  （不执行）" -ForegroundColor Yellow
    exit 0
}

$process = Start-Process -FilePath $exe -ArgumentList $profileArgs -WorkingDirectory $repo -PassThru
Write-Host "已启动，PID $($process.Id)"
exit 0
