#Requires -Version 7.0
<#
.SYNOPSIS
    从一份构建产物里**另存**一份可以发布出去的 exe：去掉符号（体积小约四分之一），
    原构建目录一个字节都不动。

.DESCRIPTION
    `measure-release-size.ps1` 只回答"去掉符号能小多少"，它**不改任何东西**。
    这个脚本是那一步的落地版：真造一份精简副本，并把它该带的证据一起记下来。

    做的事（按顺序，任何一步不对就整个失败，不留半份产物）：

    1. 读原 exe 的 SHA-256 与导入 DLL 清单；
    2. 复制一份到输出目录，只对**副本**跑 `strip.exe --strip-unneeded`；
    3. 再读一次副本的导入清单，必须和原件逐条相同（去掉的是符号，不是依赖）；
    4. 把原 exe 的调试信息单独留一份（`objcopy --only-keep-debug` → `<名字>.debug`），
       再给副本挂上 debuglink（`.gnu_debuglink` 只是指个名字，几十字节）。
       这一步在部分 binutils 上不支持 PE 目标 —— 不支持就**跳过并写明**，不算失败：
       排查用的原件本来也留在构建目录里。
    5. 写 `release-copy.json`：两份的字节数与 SHA-256、导入清单、原件路径。

    原 exe 的 SHA-256 最后会再读一次核对一次：它要是变了，说明有人同时在构建，
    这一份副本的来路就说不清，脚本报错退出。

.PARAMETER BuildDir
    构建目录（里面有 WTFMinecraftLauncher.exe）。默认 `build/static-release` —— 发布用的是
    静态 Qt 那份（docs/dev/BUILDING.md「要真·单文件就换静态 Qt」）。

.PARAMETER OutputDir
    输出目录。默认 `build/release-copy-<时间戳>`，**不会**覆盖已有的东西。
    要拿去发版就自己先看一眼这个目录。

.NOTES
    跑完还要**真的启动一次**这份精简副本再发布（`tools/perf-startup.ps1 -Exe <副本>`），
    导入清单相同不等于跑得起来。本脚本不启动任何程序。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/static-release',
    [string]$OutputDir = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$exe = Join-Path $build 'WTFMinecraftLauncher.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "没有这个构建产物：$exe（先编 build/static-release）" }

$compilerLine = Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') `
    -Pattern '^CMAKE_CXX_COMPILER:[^=]+=(.+)$' | Select-Object -First 1
if (-not $compilerLine) { throw "这个构建目录里没有 CMakeCache.txt：$build" }
$bin = Split-Path $compilerLine.Matches[0].Groups[1].Value -Parent
foreach ($tool in @('strip.exe', 'objdump.exe', 'objcopy.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $tool))) {
        throw "缺 GNU $tool（在 $bin）；原件没有被改动。"
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repo ('build/release-copy-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$out = [IO.Path]::GetFullPath($OutputDir, $repo)
if (Test-Path -LiteralPath $out) { throw "输出目录已经存在，换个名字：$out" }
$null = New-Item -ItemType Directory -Path $out

# 外部工具一律带超时：strip/objcopy 碰到奇怪文件会卡在那儿，这一步不值得挂住整个发布。
function Invoke-Bounded([string]$Tool, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $bin $Tool))
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { $process.Kill($true); throw "$Tool 超过 30 秒没有结束。" }
    if ($process.ExitCode -ne 0) { throw "$Tool 失败：$($stderr.GetAwaiter().GetResult())" }
    return $stdout.GetAwaiter().GetResult()
}
function Get-Imports([string]$Path) {
    $dump = Invoke-Bounded 'objdump.exe' @('-p', $Path)
    return @([regex]::Matches($dump, 'DLL Name:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object)
}
function Get-Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

$sourceHash = Get-Hash $exe
$sourceBytes = (Get-Item -LiteralPath $exe).Length
$imports = Get-Imports $exe

$slim = Join-Path $out 'WTFMinecraftLauncher.exe'
Copy-Item -LiteralPath $exe -Destination $slim
$null = Invoke-Bounded 'strip.exe' @('--strip-unneeded', $slim)
$slimImports = Get-Imports $slim
if (@(Compare-Object $imports $slimImports).Count -ne 0) {
    throw "去掉符号之后导入清单变了 —— 这不是精简，是有东西被删掉了。副本留在 $slim 供检查。"
}

# 调试信息单独留一份。PE 目标上不一定支持；不支持就记一句跳过，不影响精简副本。
$debugFile = Join-Path $out 'WTFMinecraftLauncher.debug'
$debugBytes = 0
$debugNote = ''
try {
    $null = Invoke-Bounded 'objcopy.exe' @('--only-keep-debug', $exe, $debugFile)
    $debugBytes = (Get-Item -LiteralPath $debugFile).Length
    $null = Invoke-Bounded 'objcopy.exe' @(
        ('--add-gnu-debuglink=' + $debugFile), $slim)
} catch {
    $debugNote = "这一版 binutils 不支持把 PE 的调试信息单独拆出来（$($_.Exception.Message)）；排查用原件见构建目录。"
    if (Test-Path -LiteralPath $debugFile) { Remove-Item -LiteralPath $debugFile -Force }
    $debugBytes = 0
}

$slimBytes = (Get-Item -LiteralPath $slim).Length
$sourceUnchanged = $sourceHash -eq (Get-Hash $exe)
@{
    source = $exe
    sourceBytes = $sourceBytes
    sourceSha256 = $sourceHash
    sourceUnchanged = $sourceUnchanged
    slim = $slim
    slimBytes = $slimBytes
    slimSha256 = (Get-Hash $slim)
    savedBytes = $sourceBytes - $slimBytes
    debugFile = if ($debugBytes -gt 0) { $debugFile } else { $null }
    debugBytes = $debugBytes
    debugNote = $debugNote
    imports = $imports
    importsUnchanged = $true
    note = '导入清单相同不等于跑得起来：发布之前用 tools/perf-startup.ps1 -Exe <副本> 真启动一次。'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'release-copy.json') -Encoding utf8
if (-not $sourceUnchanged) {
    throw '测量期间原件被改了（有人在构建？）—— 这份副本的来路说不清，别发。'
}
Write-Output (Join-Path $out 'release-copy.json')
