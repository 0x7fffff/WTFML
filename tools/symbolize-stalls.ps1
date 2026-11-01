#Requires -Version 7.0
<#
.SYNOPSIS
    把测量探针采到的"卡住时的调用栈"翻译成人话：本程序的帧补上函数名和行号。

.DESCRIPTION
    `src/perf/StallSampler.cpp` 在测量构建里盯着主线程的心跳，卡住就暂停它、
    走一遍栈。栈里本程序的帧只记 `WTFMinecraftLauncher.exe+0xRVA` ——
    MinGW 的 DWARF 只有 addr2line 认得，dbghelp 认不了（它认 PDB），
    所以名字要事后补，而且那个测量构建必须带 `-g`（CMakeLists 里已经带上）。

    这个脚本只读文件、只跑 addr2line，不启动任何程序。

.PARAMETER ProbeJson
    `tools/perf-startup.ps1 -Probe` 跑出来的 perf-probe.json（在它的报告目录/沙盒里）。

.PARAMETER Exe
    采栈的那一份 exe。默认取报告里没有的，就找 build/perf-probe/ 下那一个。

.PARAMETER Addr2Line
    addr2line.exe。默认用开发工具链那一份。

.EXAMPLE
    pwsh -File tools/symbolize-stalls.ps1 -ProbeJson build/tmp-wtfml-probe/perf-probe.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProbeJson,
    [string]$Exe = '',
    [string]$Addr2Line = 'D:/Qt/Qt/Tools/mingw1310_64/bin/addr2line.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path $PSScriptRoot -Parent
function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $repo $Path
}
$ProbeJson = Resolve-RepoPath $ProbeJson
if (-not (Test-Path -LiteralPath $ProbeJson)) { throw "没有这个报告：$ProbeJson" }

if (-not $Exe) {
    $candidates = @(Get-ChildItem -Path (Join-Path $repo 'build') -Filter 'WTFMinecraftLauncher.exe' -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'perf|probe' } | Sort-Object LastWriteTime -Descending)
    if ($candidates.Count -eq 0) { throw '找不到探针构建的 exe；用 -Exe 指定。' }
    $Exe = $candidates[0].FullName
}
$Exe = Resolve-RepoPath $Exe
if (-not (Test-Path -LiteralPath $Exe)) { throw "没有这个 exe：$Exe" }
if (-not (Test-Path -LiteralPath $Addr2Line)) { throw "没有 addr2line：$Addr2Line" }

# PE 头里的 ImageBase：addr2line 要的是 VMA（ImageBase + RVA），不是文件偏移也不是 RVA。
function Get-ImageBase([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $head = New-Object byte[] 4096
        $read = $stream.Read($head, 0, $head.Length)
        if ($read -lt 0x100) { throw "PE 头不完整：$Path" }
        $peOffset = [BitConverter]::ToInt32($head, 0x3C)
        if ($peOffset + 0x40 -ge $read) { throw "e_lfanew 离谱：0x$($peOffset.ToString('x'))" }
        if ($head[$peOffset] -ne 0x50 -or $head[$peOffset + 1] -ne 0x45) { throw "不是 PE：$Path" }
        $optional = $peOffset + 24
        $magic = [BitConverter]::ToUInt16($head, $optional)
        if ($magic -eq 0x20B) { return [BitConverter]::ToUInt64($head, $optional + 24) }   # PE32+
        if ($magic -eq 0x10B) { return [uint64][BitConverter]::ToUInt32($head, $optional + 28) } # PE32
        throw "未知的可选头 magic：0x$($magic.ToString('x'))"
    } finally {
        $stream.Dispose()
    }
}

$report = Get-Content -LiteralPath $ProbeJson -Raw | ConvertFrom-Json
$stalls = $report.stalls
if ($null -eq $stalls -or $null -eq $stalls.samples -or $stalls.samples.Count -eq 0) {
    Write-Output '这次测量没有采到栈（没有超过阈值的卡顿，或者报告来自不带采样器的构建）。'
    return
}

$imageBase = Get-ImageBase $Exe
$exeName = Split-Path $Exe -Leaf

# 所有要解析的地址攒起来一次问 addr2line（起一次进程比每条栈各起一次快得多）。
#
# 两个细节：
#  * 栈里除第一帧外都是"返回地址" —— 它指向调用指令的下一条，直接查会落到下一行
#    甚至下一个函数头上（表现为一串 `~QString()` 之类的噪声）。所以查 addr-1。
#  * 用 `-i` 让 addr2line 把内联链一起给出来，取**最后**一行就是真正写这段代码的
#    函数（前面几行是它内联进来的小函数）。
$pending = [System.Collections.Generic.List[uint64]]::new()
$samples = @($stalls.samples)
foreach ($sample in $samples) {
    $index = 0
    foreach ($frame in $sample.frames) {
        if ($frame -match ('^' + [regex]::Escape($exeName) + '\+0x([0-9a-f]+)')) {
            $address = $imageBase + [Convert]::ToUInt64($Matches[1], 16)
            if ($index -gt 0) { $address = [uint64]($address - 1) }
            $pending.Add($address)
        }
        $index++
    }
}
$resolved = @{}
if ($pending.Count -gt 0) {
    $args = @('-a', '-f', '-i', '-C', '-e', $Exe) + ($pending | ForEach-Object { '0x{0:x}' -f $_ })
    $lines = & $Addr2Line @args
    $current = $null
    $functions = [System.Collections.Generic.List[string]]::new()
    $locations = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if ($line -match '^0x([0-9a-fA-F]+)$') {
            if ($null -ne $current -and $functions.Count -gt 0) {
                $function = $functions[$functions.Count - 1]
                $location = $locations[$locations.Count - 1]
                $resolved[$current] = if ($function -eq '??') { $location } else { "$function  $location" }
            }
            $current = '0x{0:x}' -f [Convert]::ToUInt64($Matches[1], 16)
            $functions.Clear()
            $locations.Clear()
            continue
        }
        if ($null -eq $current) { continue }
        if ($functions.Count -eq $locations.Count) { $functions.Add($line) } else { $locations.Add($line) }
    }
    if ($null -ne $current -and $functions.Count -gt 0) {
        $function = $functions[$functions.Count - 1]
        $location = $locations[$locations.Count - 1]
        $resolved[$current] = if ($function -eq '??') { $location } else { "$function  $location" }
    }
}

# 同一份栈只讲一次，出现次数写在后面。
$seen = @{}
$ordered = foreach ($sample in $samples) {
    $key = ($sample.frames -join '>')
    if ($seen.ContainsKey($key)) { $seen[$key].Count++; continue }
    $entry = [pscustomobject]@{ GapMs = $sample.gapMs; Step = $sample.step; Frames = $sample.frames; Count = 1 }
    $seen[$key] = $entry
    $entry
}

Write-Output "# 卡住的调用栈（$Exe）"
Write-Output ''
Write-Output '阈值见报告里 stalls.samples 的来源：心跳停 50 ms 以上才算一次卡顿；同一段卡顿只采一份。'
Write-Output ''
foreach ($entry in $ordered) {
    Write-Output ("## {0} ms / 步骤 {1}{2}" -f $entry.GapMs, $entry.Step,
        $(if ($entry.Count -gt 1) { "（同样的栈出现 $($entry.Count) 次）" } else { '' }))
    $index = 0
    foreach ($frame in $entry.Frames) {
        if ($frame -match ('^' + [regex]::Escape($exeName) + '\+0x([0-9a-f]+)')) {
            $raw = $imageBase + [Convert]::ToUInt64($Matches[1], 16)
            if ($index -gt 0) { $raw = [uint64]($raw - 1) }
            $address = '0x{0:x}' -f $raw
            $name = if ($resolved.ContainsKey($address)) { $resolved[$address] } else { '??' }
            Write-Output ("  {0}  →  {1}" -f $frame, $name)
        } else {
            Write-Output ("  {0}" -f $frame)
        }
        $index++
    }
    Write-Output ''
}
