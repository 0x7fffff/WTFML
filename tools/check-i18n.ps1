#Requires -Version 7.0
<#
.SYNOPSIS
    守卫：src/ 与 ui/ 里含中文、却没包进 tr() / translate() / QT_TR_NOOP 之类里的字符串字面量，
    不许比基线多。

.DESCRIPTION
    源语言是中文，所以界面上的每一句中文都得**能被翻译**才会出现在英文界面里。第 2 期之前
    这类字面量还有几千处（见 tools/i18n-baseline.txt），一次清零不现实；这一层守的是
    "新代码不许再加"：逐文件比计数，只要有一个文件比基线多就失败。

    判据（逐条都是"这处文面上的中文能不能被 lupdate 收走"）：
      1. 只看 .cpp / .hpp / .h；
      2. 字符串字面量里含中日韩汉字或全角标点才算；
      3. 字面量在一个包装调用的括号里就不算：tr / translate / QT_TR_NOOP /
         QT_TRANSLATE_NOOP / QT_TRANSLATE_NOOP3 / QT_TRANSLATE_NOOP_UTF8 /
         QT_TR_NOOP_UTF8（`QCoreApplication::translate("ctx", "中文")` 的两个实参都算）；
      4. 注释里的不算；`#include` 后面那个文件名不算；
      5. 行尾写 `// i18n-ignore` 的那一行整行不算 —— 这是"这处不翻"的唯一标记，
         用途：日志、内部提示词、语言名本身。文档见 docs/dev/I18N.md。

    限制：按**行**扫，跨行块注释有状态、跨行字符串字面量没有（本仓没有用那种写法）。
    遇到认不出的写法（宏拼串之类）按"没包"算，宁可多报一次。

.PARAMETER RepoRoot
    仓库根目录。默认是脚本所在目录的上一级。

.PARAMETER UpdateBaseline
    把当前计数写回 tools/i18n-baseline.txt（第 2 期降基线时用），不判失败。

.PARAMETER SelfTest
    在临时目录里造样本，验证上面那几条判据本身（不查真实仓库）。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [switch]$UpdateBaseline,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$Marker = 'i18n-ignore'
$Wrappers = 'tr', 'translate', 'QT_TR_NOOP', 'QT_TRANSLATE_NOOP', 'QT_TRANSLATE_NOOP3',
    'QT_TRANSLATE_NOOP_UTF8', 'QT_TR_NOOP_UTF8'
# 汉字（基本区 + 扩展 A + 兼容区）与全角标点：这两类都是"得翻"的字。
$CjkPattern = '[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\u3000-\u303f\uff00-\uffef]'
$TokenPattern = '"(?:\\.|[^"\\])*"|//.*$|/\*|\*/'
$TokenRegex = [regex]::new($TokenPattern)

# 逐行扫一个文件，返回"含中文且没包起来"的字面量条数。
function Measure-File([string]$Path) {
    $count = 0
    $inBlock = $false
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match ('^\s*#\s*include')) { continue }
        $lineIgnored = $line.Contains($Marker)
        $pos = 0
        while ($pos -lt $line.Length) {
            $m = $TokenRegex.Match($line, $pos)
            if (-not $m.Success) { break }
            $tok = $m.Value
            if ($inBlock) {
                if ($tok -eq '*/') { $inBlock = $false }
                $pos = $m.Index + $m.Length
                continue
            }
            if ($tok -eq '/*') { $inBlock = $true; $pos = $m.Index + $m.Length; continue }
            if ($tok.StartsWith('//')) { break }
            # 字符串字面量
            if (-not $lineIgnored -and $m.Value -match $CjkPattern) {
                $prefix = $line.Substring(0, $m.Index)
                if (-not (Test-Wrapped $prefix)) { $count++ }
            }
            $pos = $m.Index + $m.Length
        }
    }
    return $count
}

# 字面量前面那截文本里，最近一个还没闭合的 '(' 是谁的：是包装函数就算包住了。
function Test-Wrapped([string]$Prefix) {
    $depth = 0
    for ($i = $Prefix.Length - 1; $i -ge 0; $i--) {
        $c = $Prefix[$i]
        if ($c -eq ')') { $depth++ }
        elseif ($c -eq '(') {
            if ($depth -gt 0) { $depth--; continue }
            $head = $Prefix.Substring(0, $i)
            $name = [regex]::Match($head, '([A-Za-z_][A-Za-z0-9_]*)\s*$').Groups[1].Value
            return $Wrappers -contains $name
        }
    }
    return $false
}

function Get-Counts([string]$Root) {
    $files = Get-ChildItem -LiteralPath $Root -Recurse -File -Include '*.cpp', '*.hpp', '*.h' |
        Where-Object { $_.FullName -notmatch '[\/](build|_tmp|_archive)[\/]' }
    $map = @{}
    foreach ($file in $files) {
        $text = [IO.File]::ReadAllText($file.FullName)
        if ($text -notmatch $CjkPattern) { continue }
        $n = Measure-File $file.FullName
        if ($n -gt 0) {
            $rel = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace([char]92, [char]47)
            $map[$rel] = $n
        }
    }
    # 只守 src/ 与 ui/：别的目录（tests、tools）不在此列。
    $filtered = @{}
    foreach ($key in $map.Keys) {
        if ($key -like 'src/*' -or $key -like 'ui/*') { $filtered[$key] = $map[$key] }
    }
    return $filtered
}

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('i18n-selftest-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $tmp 'src') -Force | Out-Null
    $sample = Join-Path $tmp 'src/sample.cpp'
    @'
// 注释里的中文不算
/* 块注释
   里的中文也不算 */
#include "中文头文件.hpp"
void f() {
    tr("要翻");
    QCoreApplication::translate("Ctx", "也要翻");
    QT_TR_NOOP("照样翻");
    auto a = QStringLiteral("没包");          // 计 1
    auto b = QStringLiteral("不翻这处");      // i18n-ignore
    auto c = tr("包了 %1").arg(QStringLiteral("还是没包"));  // 「还是没包」计 1
}
'@ | Set-Content -LiteralPath $sample -Encoding utf8
    $got = (Get-Counts $tmp)['src/sample.cpp']
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    if ($got -ne 2) { Write-Host "FAIL 自测：期望 2 处未包，实际 $got"; exit 1 }
    Write-Host 'PASS 自测：注释 / include / 包装调用 / 不翻标记都不计，裸字面量计 2'
    exit 0
}

if (-not $RepoRoot) { $RepoRoot = Split-Path $PSScriptRoot -Parent }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$baselinePath = Join-Path $PSScriptRoot 'i18n-baseline.txt'
$now = Get-Counts $RepoRoot

if ($UpdateBaseline) {
    $lines = $now.Keys | Sort-Object | ForEach-Object { "$_`t$($now[$_])" }
    [IO.File]::WriteAllLines($baselinePath, $lines)
    $sum = ($now.Values | Measure-Object -Sum).Sum
    Write-Host ("基线已写回：{0} 个文件、{1} 处未包中文" -f $now.Count, $sum)
    exit 0
}

$baseline = @{}
if (Test-Path -LiteralPath $baselinePath) {
    foreach ($line in [IO.File]::ReadAllLines($baselinePath)) {
        if (-not $line.Trim()) { continue }
        $parts = $line -split "`t"
        if ($parts.Count -eq 2) { $baseline[$parts[0]] = [int]$parts[1] }
    }
}

$bad = [Collections.Generic.List[string]]::new()
foreach ($file in $now.Keys) {
    $allowed = if ($baseline.ContainsKey($file)) { $baseline[$file] } else { 0 }
    if ($now[$file] -gt $allowed) {
        $bad.Add("$file  未包中文 $($now[$file]) 处（基线 $allowed）")
    }
}
$total = ($now.Values | Measure-Object -Sum).Sum
$baseTotal = ($baseline.Values | Measure-Object -Sum).Sum
Write-Host ("未包中文：{0} 个文件、{1} 处（基线 {2} 处）" -f $now.Count, $total, $baseTotal)
if ($bad.Count -gt 0) {
    Write-Host '比基线多（新代码里的中文要包 tr()，或行尾标 // i18n-ignore）：'
    $bad | Sort-Object | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host 'PASS 没有新增未包中文'
exit 0
