#Requires -Version 7.0
<#
.SYNOPSIS
    构建耗时基线：冷配置、全量冷构建、空转增量、单文件、高扇出头文件、纯链接。

.DESCRIPTION
    P2「测量＋优化｜构建与运行」的第一半。只测量、不改产品行为；每次跑都落到
    build/perf-build-<时间戳>/ 下（baseline.json + summary.md + 每步原始日志）。

    为什么必须是**全新目录**：CMake/Make 的增量状态是"上一次编过什么"的全部记忆。
    复用一个旧目录量出来的"冷构建"只是空转。目录已存在且非空时默认拒绝，
    要复用请显式 -Reuse（那一跑的 cold-full 名不副实，会写进 JSON 的 notes）。

    扇出/纯链接两步会改源文件时间戳（不改内容）。这是 Make 认"要不要重编"的唯一依据，
    跑完 git status 是干净的。

.PARAMETER BuildDir
    构建目录。默认 build/perf-build-<时间戳>，跑完保留作为证据。

.PARAMETER Jobs
    并行度，默认 8。写进 JSON；不同并行度的数字不可横向比较。

.PARAMETER Reuse
    允许复用非空构建目录（cold-full 将不再是冷构建）。

.PARAMETER SkipCold
    跳过冷配置+冷全量（只量增量几步；要求目录里已经有完整构建）。

.EXAMPLE
    pwsh -File tools/perf-build.ps1 -Jobs 8
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [int]$Jobs = 8,
    [switch]$Reuse,
    [switch]$SkipCold,
    # 只编产品、不编 38 个验证目标。默认带测试（CI 和日常验证就是这么编的）；
    # 这一档用来分清楚"冷构建那 450 秒里有多少是产品的、多少是验证工程的"。
    [switch]$ProductOnly,
    [string]$CMake = 'cmake',
    [string]$QtPrefix = 'D:/Qt/Qt/6.10.1/mingw_64',
    [string]$Compiler = 'D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$LASTEXITCODE = 0

$repo = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build/perf-build-$stamp" }
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)

if ((Test-Path $BuildDir) -and (Get-ChildItem -Force $BuildDir | Select-Object -First 1)) {
    if (-not $Reuse) {
        throw "构建目录非空：$BuildDir。冷构建要在全新目录里跑；确实要复用请加 -Reuse。"
    }
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$reportDir = Join-Path $repo "build/perf-report-$stamp"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

# 编译器/工具链放进子进程 PATH，和 CI、本地惯用命令保持一致。
$env:PATH = (@($QtPrefix + '/bin', (Split-Path $Compiler), $env:PATH) -join ';')

$steps = [Collections.Generic.List[object]]::new()

function Measure-Step {
    param(
        [string]$Name,
        [scriptblock]$Action,
        [string]$LogName,
        [string]$Note = ''
    )
    $log = Join-Path $reportDir ($LogName + '.log')
    Write-Host ">>> $Name" -NoNewline
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $code = 0
    try {
        & $Action *> $log
        if ($LASTEXITCODE -ne $null) { $code = [int]$LASTEXITCODE }
    } catch {
        $_ | Out-String | Add-Content -Path $log -Encoding utf8
        $code = 1
    }
    $sw.Stop()
    $text = if (Test-Path $log) { Get-Content -LiteralPath $log -Raw -Encoding utf8 } else { '' }
    $compiled = ([regex]::Matches($text, 'Building CXX object')).Count
    $linkedExe = ([regex]::Matches($text, 'Linking CXX executable')).Count
    $linkedLib = ([regex]::Matches($text, 'Linking CXX static library')).Count
    $entry = [ordered]@{
        name        = $Name
        ok          = ($code -eq 0)
        exitCode    = $code
        seconds     = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        compiledTUs = $compiled
        linkedExes  = $linkedExe
        linkedLibs  = $linkedLib
        log         = [IO.Path]::GetRelativePath($repo, $log)
        note        = $Note
    }
    $steps.Add([pscustomobject]$entry)
    $state = if ($code -eq 0) { 'ok' } else { "FAIL($code)" }
    Write-Host ("  {0}  {1:N1}s  TUs={2}  links={3}+{4}" -f $state, $sw.Elapsed.TotalSeconds, $compiled, $linkedExe, $linkedLib)
    if ($code -ne 0) {
        Write-Warning "步骤失败：$Name，日志：$log"
    }
}

$gitCommit = (& git -C $repo rev-parse --short HEAD 2>$null)
$gitDirty = [bool](& git -C $repo status --porcelain 2>$null)
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$os = (Get-CimInstance Win32_OperatingSystem).Caption
$qtVersion = (& (Join-Path $QtPrefix 'bin/qmake.exe') -query QT_VERSION 2>$null)
$compilerVersion = (& $Compiler --version 2>$null | Select-Object -First 1)

$notes = [Collections.Generic.List[string]]::new()
if ($ProductOnly) { $notes.Add('只编产品（WTFML_BUILD_TESTS=OFF）：用来和带测试的那一跑区分产品/验证工程的份额。') }
if ($Reuse) { $notes.Add('复用了已有构建目录：cold-full 不是真正的冷构建。') }
if ($SkipCold) { $notes.Add('跳过了冷配置与冷全量。') }
if ($gitDirty) { $notes.Add('工作区有未提交改动：数字只对应当前工作区。') }

$coldDirWasEmpty = -not (Test-Path $BuildDir) -or -not (Get-ChildItem -Force $BuildDir | Select-Object -First 1)

if (-not $SkipCold) {
    Measure-Step -Name 'configure' -LogName '01-configure' -Action {
        & $CMake -S $repo -B $BuildDir -G 'MinGW Makefiles' `
            '-DCMAKE_BUILD_TYPE=Release' `
            "-DCMAKE_CXX_COMPILER=$Compiler" `
            "-DCMAKE_PREFIX_PATH=$QtPrefix" `
            $(if ($ProductOnly) { '-DWTFML_BUILD_TESTS=OFF' } else { '-DWTFML_BUILD_TESTS=ON' }) `
            '-DWTFML_STRICT_WARNINGS=ON' `
            '-DWTFML_WARNINGS_AS_ERRORS=ON'
    }
    if (-not $steps[-1].ok) { throw '配置失败，后面的数字没有意义。' }
    $coldNote = '全新目录' + $(if ($coldDirWasEmpty) { '' } else { '（非空，见 notes）' })
    Measure-Step -Name 'cold-full' -LogName '02-cold-full' -Note $coldNote -Action {
        & $CMake --build $BuildDir --parallel $Jobs
    }
} else {
    if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
        throw '-SkipCold 要求目标目录里已经有配置好的构建。'
    }
}

Measure-Step -Name 'incremental-noop' -LogName '03-noop' -Action {
    & $CMake --build $BuildDir --parallel $Jobs
}

function Touch-Source([string]$RelativePath) {
    $full = Join-Path $repo $RelativePath
    if (-not (Test-Path $full)) { throw "没有这个文件：$RelativePath" }
    (Get-Item -LiteralPath $full).LastWriteTime = Get-Date
}

Measure-Step -Name 'leaf-cpp' -LogName '04-leaf-cpp' -Note 'src/util/SystemMemory.cpp（叶子实现）' -Action {
    Touch-Source 'src/util/SystemMemory.cpp'
    & $CMake --build $BuildDir --parallel $Jobs
}

Measure-Step -Name 'fanout-header' -LogName '05-fanout-header' -Note 'src/util/AppPaths.hpp（高扇出头）' -Action {
    Touch-Source 'src/util/AppPaths.hpp'
    & $CMake --build $BuildDir --parallel $Jobs
}

Measure-Step -Name 'big-tu' -LogName '06-big-tu' -Note 'src/MainWindow.cpp（最大翻译单元）' -Action {
    Touch-Source 'src/MainWindow.cpp'
    & $CMake --build $BuildDir --parallel $Jobs
}

Measure-Step -Name 'link-only' -LogName '07-link-only' -Note '删掉主程序 exe 后只做链接' -Action {
    $exe = Join-Path $BuildDir 'WTFMinecraftLauncher.exe'
    if (Test-Path $exe) { Remove-Item -LiteralPath $exe -Force }
    & $CMake --build $BuildDir --target WTFMinecraftLauncher --parallel $Jobs
}

$objCount = @(Get-ChildItem -Path $BuildDir -Filter '*.obj' -Recurse -ErrorAction SilentlyContinue).Count
$sizeGb = [math]::Round(((Get-ChildItem -Path $BuildDir -Recurse -File -ErrorAction SilentlyContinue |
    Measure-Object -Property Length -Sum).Sum) / 1GB, 2)

$baseline = [ordered]@{
    schema           = 1
    stamp            = $stamp
    host             = $env:COMPUTERNAME
    os               = $os
    cpu              = $cpu
    ramGb            = $ramGb
    commit           = $gitCommit
    worktreeDirty    = $gitDirty
    buildDir         = [IO.Path]::GetRelativePath($repo, $BuildDir)
    generator        = 'MinGW Makefiles'
    buildType        = 'Release'
    testsOn          = -not $ProductOnly
    warningsAsErrors = $true
    jobs             = $Jobs
    compiler         = $compilerVersion
    qtVersion        = $qtVersion
    objectCount      = $objCount
    buildDirGb       = $sizeGb
    steps            = $steps
    notes            = $notes
}

$jsonPath = Join-Path $reportDir 'baseline.json'
$baseline | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$lines = [Collections.Generic.List[string]]::new()
$lines.Add("# 构建耗时基线 $stamp")
$lines.Add('')
$lines.Add("- 机器：$cpu / ${ramGb} GB / $os")
$lines.Add("- 工具链：$compilerVersion；Qt $qtVersion；$($baseline.generator)；$($baseline.buildType)")
$lines.Add("- 并行：$Jobs；提交：$gitCommit$(if ($gitDirty) { '（工作区有改动）' })")
$lines.Add("- 构建目录：$($baseline.buildDir)；对象 $objCount 个，共 $sizeGb GB")
$lines.Add('')
$lines.Add('| 步骤 | 结果 | 秒 | 重编 TU | 链接 exe | 链接静态库 |')
$lines.Add('| --- | --- | ---: | ---: | ---: | ---: |')
foreach ($s in $steps) {
    $lines.Add(("| {0} | {1} | {2:N1} | {3} | {4} | {5} |" -f $s.name, $(if ($s.ok) { '通过' } else { "失败($($s.exitCode))" }), $s.seconds, $s.compiledTUs, $s.linkedExes, $s.linkedLibs))
}
if ($notes.Count) {
    $lines.Add('')
    $lines.Add('说明：')
    foreach ($n in $notes) { $lines.Add("- $n") }
}
$summaryPath = Join-Path $reportDir 'summary.md'
$lines | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host ''
Write-Host "报告：$jsonPath"
Write-Host "摘要：$summaryPath"
if (@($steps | Where-Object { -not $_.ok }).Count) { exit 1 }
exit 0
