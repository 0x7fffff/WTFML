#Requires -Version 7.0
<#
.SYNOPSIS
    检查新写的对话框 / showPage 页面有没有登记进 sweep 的表面清单。

.DESCRIPTION
    设计系统是逐控件接进来的，sweep（CTest `wtfml.surface-sweep`）靠
    `tests/ui_check/surface-registry.txt` 这份表面清单说明自己扫过哪些入口。新写一页却忘了
    登记，sweep 不会红、也没人会注意到 —— 这个脚本就是补这一条。规则见
    docs/dev/TESTING.md「表面登记」。

    逐条检查：

    1. `ui/`、`src/` 里每个 QDialog / ChromeDialog / GlassPopover 的子类（含间接继承），
       都要在表面清单里有一行；确实不该进 sweep 的，在类声明那一行行尾写
       `// surface-registry-ignore: 理由`；
    2. 就地 new / 栈上建的裸 QDialog / ChromeDialog / GlassPopover（不是子类）要按
       `类名#objectName` 登记；
    3. 每处 `overlay->showPage(...)` / `host.showPage(...)` 的第一参数要能认出一个已登记的
       页面类；认不出来的在调用那一行写豁免注释；
    4. 清单里的每个表面键都必须在 `UiSurfaceSweepCheck.cpp` 里真的 record() 过，
       清单里的每个入口也必须在代码里真的存在（防止登记了不扫、或登记名写错）。

    检查是**只读**的：不改任何文件，只列违规项。有违规就退出码 1。

.PARAMETER RepoRoot
    仓库根目录。默认是脚本所在目录的上一级。

.PARAMETER SelfTest
    不查真实仓库，改在临时目录里造样本，验证上面几类检测逻辑本身。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$IgnoreMarker = 'surface-registry-ignore:'
$SeedBases = @('QDialog', 'ChromeDialog', 'GlassPopover')
$RegistryRelative = 'tests/ui_check/surface-registry.txt'
$SweepRelative = 'tests/ui_check/modules/UiSurfaceSweepCheck.cpp'

function Get-SourceFiles([string]$Root) {
    foreach ($dir in @('ui', 'src')) {
        $path = Join-Path $Root $dir
        if (-not (Test-Path -LiteralPath $path)) { continue }
        Get-ChildItem -LiteralPath $path -Recurse -File |
            Where-Object { $_.Extension -in @('.hpp', '.cpp') }
    }
}

function Get-LineNumber([string]$Text, [int]$Index) {
    if ($Index -le 0) { return 1 }
    ($Text.Substring(0, $Index) -split "`n").Count
}

# 行尾豁免：`// surface-registry-ignore: 理由`。理由不能是空的。
function Test-IgnoreComment([string]$Line) {
    if (-not $Line) { return $false }
    $m = [regex]::Match($Line, '//\s*' + [regex]::Escape($IgnoreMarker) + '\s*(\S.*)$')
    return $m.Success -and $m.Groups[1].Value.Trim().Length -ge 4
}

# 读表面清单：`表面键 | 入口（逗号分隔） | 说明`。
function Read-Registry([string]$Path) {
    $rows = [Collections.Generic.List[object]]::new()
    $lines = [IO.File]::ReadAllLines($Path)
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $text = $lines[$i].Trim()
        if (-not $text -or $text.StartsWith('#')) { continue }
        $parts = $text -split '\|'
        if ($parts.Count -lt 2) { throw "${RegistryRelative}:$($i + 1) 格式不对（要「表面键 | 入口 | 说明」）：$text" }
        $entries = @($parts[1] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if (-not $parts[0].Trim() -or $entries.Count -eq 0) {
            throw "${RegistryRelative}:$($i + 1) 表面键或入口是空的：$text"
        }
        $rows.Add([pscustomobject]@{
            Key     = $parts[0].Trim()
            Entries = $entries
            Line    = $i + 1
        })
    }
    , $rows.ToArray()
}

# 类声明表：名字、基类（去掉命名空间）、位置、那一行的原文。
function Get-ClassDeclarations([string]$Root) {
    $pattern = '(?m)^[ \t]*(?:class|struct)\s+([A-Za-z_]\w*)\b[^;{]*?:\s*(?:public|protected|private)\s+([A-Za-z_]\w*(?:::\w+)*)'
    $classes = [Collections.Generic.List[object]]::new()
    foreach ($file in Get-SourceFiles $Root) {
        $text = [IO.File]::ReadAllText($file.FullName)
        $lines = $text -split "`r?`n"
        foreach ($m in [regex]::Matches($text, $pattern)) {
            $line = Get-LineNumber $text $m.Index
            $classes.Add([pscustomobject]@{
                Name     = $m.Groups[1].Value
                Base     = ($m.Groups[2].Value -split '::')[-1]
                Relative = (([IO.Path]::GetRelativePath($Root, $file.FullName)) -replace '\\', '/')
                Line     = $line
                LineText = if ($line -le $lines.Count) { $lines[$line - 1] } else { '' }
            })
        }
    }
    , $classes.ToArray()
}

# 所有的类名（含没有基类的页、前向声明）：核对清单里的入口名有没有写错。
function Get-KnownClassNames([string]$Root) {
    $names = [Collections.Generic.HashSet[string]]::new()
    foreach ($file in Get-SourceFiles $Root) {
        $text = [IO.File]::ReadAllText($file.FullName)
        foreach ($m in [regex]::Matches($text, '(?m)^[ \t]*(?:class|struct)\s+([A-Za-z_]\w*)\b')) {
            [void]$names.Add($m.Groups[1].Value)
        }
    }
    , $names
}

function Get-Ancestors([string]$Name, [hashtable]$BaseOf) {    $seen = [Collections.Generic.HashSet[string]]::new()
    while ($BaseOf.ContainsKey($Name)) {
        $base = $BaseOf[$Name]
        if (-not $seen.Add($base)) { break }
        $Name = $base
    }
    , @($seen)
}

function Test-IsDialogClass([string]$Name, [hashtable]$BaseOf) {
    if ($Name -in $SeedBases) { return $true }
    $ancestors = Get-Ancestors $Name $BaseOf
    foreach ($a in $ancestors) { if ($a -in $SeedBases) { return $true } }
    return $false
}

# 变量声明：先本文件、再同目录文件（成员变量多在配套 .hpp 里）。返回最后一个对话框类候选。
function Find-VariableType([string]$Root, [string]$File, [string]$Name, [int]$BeforeIndex, [hashtable]$BaseOf) {
    $dir = Split-Path $File -Parent
    $files = [Collections.Generic.List[string]]::new()
    $files.Add($File)
    $baseName = [IO.Path]::GetFileNameWithoutExtension($File)
    foreach ($ext in @('.hpp', '.cpp')) {
        $sibling = Join-Path $dir ($baseName + $ext)
        if ((Test-Path -LiteralPath $sibling) -and $sibling -ne $File) { $files.Add($sibling) }
    }
    foreach ($other in Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in @('.hpp', '.cpp') }) {
        if ($other.FullName -notin $files) { $files.Add($other.FullName) }
    }
    $escaped = [regex]::Escape($Name)
    $patterns = @(
        "auto\s*\*?\s*$escaped\s*=\s*new\s+([A-Za-z_]\w*)",
        "QPointer\s*<\s*([A-Za-z_]\w*)\s*>\s*$escaped\b",
        "(?<![\w:])([A-Z][A-Za-z_]\w*)\s*(?:\*|&)\s*$escaped\b",
        "(?<![\w:])([A-Z][A-Za-z_]\w*)\s+$escaped\s*(?:\(|;|=|\{|,)"
    )
    $found = $null
    foreach ($path in $files) {
        $text = [IO.File]::ReadAllText($path)
        $scope = if ($path -eq $File) { $text.Substring(0, [Math]::Min($BeforeIndex, $text.Length)) } else { $text }
        foreach ($p in $patterns) {
            foreach ($m in [regex]::Matches($scope, $p)) {
                $type = $m.Groups[1].Value
                if (Test-IsDialogClass $type $BaseOf) { $found = $type }
            }
        }
    }
    $found
}

# 拆出 showPage( 里的第一参数（不数括号、引号里的逗号）。
function Get-FirstArgument([string]$Inner) {
    $depth = 0
    $inString = $false
    $builder = [Text.StringBuilder]::new()
    for ($i = 0; $i -lt $Inner.Length; ++$i) {
        $ch = $Inner[$i]
        if ($inString) {
            [void]$builder.Append($ch)
            if ($ch -eq '\' -and $i + 1 -lt $Inner.Length) { ++$i; [void]$builder.Append($Inner[$i]) }
            elseif ($ch -eq '"') { $inString = $false }
            continue
        }
        if ($ch -eq '"') { $inString = $true; [void]$builder.Append($ch); continue }
        if ($ch -in @('(', '[', '{', '<')) { ++$depth }
        elseif ($ch -in @(')', ']', '}', '>')) { --$depth }
        elseif ($ch -eq ',' -and $depth -eq 0) { break }
        [void]$builder.Append($ch)
    }
    $builder.ToString().Trim()
}

function Get-CallInnerText([string]$Text, [int]$OpenParenIndex) {
    $depth = 1
    $inString = $false
    for ($i = $OpenParenIndex + 1; $i -lt $Text.Length; ++$i) {
        $ch = $Text[$i]
        if ($inString) {
            if ($ch -eq '\') { ++$i }
            elseif ($ch -eq '"') { $inString = $false }
            continue
        }
        if ($ch -eq '"') { $inString = $true; continue }
        if ($ch -eq '(') { ++$depth }
        elseif ($ch -eq ')') {
            --$depth
            if ($depth -eq 0) { return $Text.Substring($OpenParenIndex + 1, $i - $OpenParenIndex - 1) }
        }
    }
    $null
}

function Find-ObjectName([string]$Text, [string]$Variable) {
    $m = [regex]::Match($Text,
        [regex]::Escape($Variable) + '\s*(?:->|\.)\s*setObjectName\s*\(\s*(?:QStringLiteral\s*\(\s*)?"([^"]+)"')
    if ($m.Success) { return $m.Groups[1].Value }
    $null
}

# 表面键比较：清单里的 `*` 当通配（sweep 里的 `%1` 也换成 `*`）。
function Test-KeyCovered([string]$RegistryKey, [string[]]$RecordedKeys) {
    foreach ($recorded in $RecordedKeys) {
        if ($RegistryKey -eq $recorded) { return $true }
        if ($RegistryKey.EndsWith('*')) {
            $prefix = $RegistryKey.Substring(0, $RegistryKey.Length - 1)
            if ($recorded.StartsWith($prefix)) { return $true }
        }
    }
    $false
}

function Invoke-SurfaceRegistryCheck([string]$Root) {
    $problems = [Collections.Generic.List[string]]::new()
    $registryPath = Join-Path $Root $RegistryRelative
    $sweepPath = Join-Path $Root $SweepRelative
    if (-not (Test-Path -LiteralPath $registryPath)) { throw "找不到表面清单：$RegistryRelative" }
    if (-not (Test-Path -LiteralPath $sweepPath)) { throw "找不到 sweep 模块：$SweepRelative" }

    $registry = Read-Registry $registryPath
    $registeredClasses = [Collections.Generic.HashSet[string]]::new()
    $registeredInline = [Collections.Generic.HashSet[string]]::new()
    foreach ($row in $registry) {
        foreach ($entry in $row.Entries) {
            if ($entry.Contains('#')) { [void]$registeredInline.Add($entry) }
            else { [void]$registeredClasses.Add($entry) }
        }
    }

    $classes = Get-ClassDeclarations $Root
    $baseOf = @{}
    foreach ($c in $classes) { if (-not $baseOf.ContainsKey($c.Name)) { $baseOf[$c.Name] = $c.Base } }

    $candidates = [ordered]@{}
    foreach ($c in $classes) {
        if ($c.Name -in $SeedBases) { continue }
        if (-not (Test-IsDialogClass $c.Name $baseOf)) { continue }
        if (-not $candidates.Contains($c.Name)) { $candidates[$c.Name] = $c }
    }

    # 1. 对话框子类要登记（或在声明行豁免）。
    foreach ($name in $candidates.Keys) {
        $c = $candidates[$name]
        if ($registeredClasses.Contains($name)) { continue }
        if (Test-IgnoreComment $c.LineText) { continue }
        $problems.Add("$($c.Relative):$($c.Line)  对话框子类 $name 没登记进 ${RegistryRelative}；" +
            "加一行「表面键 | $name | 说明」，或在类声明行尾写「// ${IgnoreMarker} 理由」")
    }

    # 2. 就地 new / 栈上建的裸 QDialog / ChromeDialog / GlassPopover：按 objectName 登记。
    $inlinePattern = '(?m)(?:new\s+(?<type>QDialog|ChromeDialog|GlassPopover)\s*[\(\{])|(?<![\w:])(?<type2>QDialog|ChromeDialog|GlassPopover)\s+(?<var>[A-Za-z_]\w*)\s*[\(\{;]'
    foreach ($file in Get-SourceFiles $Root) {
        $text = [IO.File]::ReadAllText($file.FullName)
        $lines = $text -split "`r?`n"
        foreach ($m in [regex]::Matches($text, $inlinePattern)) {
            $type = if ($m.Groups['type'].Success) { $m.Groups['type'].Value } else { $m.Groups['type2'].Value }
            $variable = $null
            if ($m.Groups['var'].Success) {
                $variable = $m.Groups['var'].Value
            } else {
                $lineStart = $text.LastIndexOf("`n", [Math]::Max(0, $m.Index - 1)) + 1
                $before = $text.Substring($lineStart, $m.Index - $lineStart)
                $assign = [regex]::Match($before, '(?<var>[A-Za-z_]\w*)\s*=\s*$')
                if ($assign.Success) { $variable = $assign.Groups['var'].Value }
            }
            $line = Get-LineNumber $text $m.Index
            $lineText = if ($line -le $lines.Count) { $lines[$line - 1] } else { '' }
            if (Test-IgnoreComment $lineText) { continue }
            $objectName = if ($variable) { Find-ObjectName $text $variable } else { $null }
            if (-not $objectName) {
                $relative = ([IO.Path]::GetRelativePath($Root, $file.FullName)) -replace '\\', '/'
                $problems.Add("${relative}:${line}  就地建的 $type 认不出 objectName，没法核对登记；" +
                    "给它 setObjectName()，或在那一行写「// ${IgnoreMarker} 理由」")
                continue
            }
            $token = "$type#$objectName"
            if (-not $registeredInline.Contains($token)) {
                $relative = ([IO.Path]::GetRelativePath($Root, $file.FullName)) -replace '\\', '/'
                $problems.Add("${relative}:${line}  $type（$objectName）没登记进 ${RegistryRelative}；" +
                    "加一行「surface key | $token | 说明」，或写「// ${IgnoreMarker} 理由」")
            }
        }
    }

    # 3. showPage 的第一参数要认得出一个已登记的页面类。
    $callPattern = 'showPage\s*\('
    foreach ($file in Get-SourceFiles $Root) {
        $text = [IO.File]::ReadAllText($file.FullName)
        $lines = $text -split "`r?`n"
        foreach ($m in [regex]::Matches($text, $callPattern)) {
            $lineStart = $text.LastIndexOf("`n", [Math]::Max(0, $m.Index - 1)) + 1
            $prefix = $text.Substring($lineStart, $m.Index - $lineStart)
            if ($prefix -match '^\s*(//|/\*|\*)') { continue }
            # 只认 OverlayHost 那一族的接收者；InstanceSettingsDialog::showPage(key) 换个页不算入口。
            if ($prefix -notmatch '(?i)(?:overlay|host)\s*(?:\(\s*\))?\s*(?:->|\.)\s*$') { continue }
            $line = Get-LineNumber $text $m.Index
            $lineText = if ($line -le $lines.Count) { $lines[$line - 1] } else { '' }
            if (Test-IgnoreComment $lineText) { continue }
            $openParen = $m.Index + $m.Length - 1
            $inner = Get-CallInnerText $text $openParen
            if ($null -eq $inner) { continue }
            $argument = Get-FirstArgument $inner
            $relative = ([IO.Path]::GetRelativePath($Root, $file.FullName)) -replace '\\', '/'
            $type = $null
            $variable = $null
            if ($argument -match '^new\s+([A-Za-z_]\w*(?:::\w+)*)') {
                $type = ($Matches[1] -split '::')[-1]
            } elseif ($argument -match '^&?\s*([A-Za-z_]\w*)\s*$') {
                $variable = $Matches[1]
                $type = Find-VariableType $Root $file.FullName $variable $m.Index $baseOf
            }
            if (-not $type) {
                $problems.Add("${relative}:${line}  showPage 的第一参数「$argument」认不出页面类；" +
                    "改成显式变量 / new 表达式，或在那一行写「// ${IgnoreMarker} 理由」")
                continue
            }
            if ($type -in $SeedBases) {
                $objectName = if ($variable) { Find-ObjectName $text $variable } else { $null }
                $token = if ($objectName) { "$type#$objectName" } else { $null }
                if (-not $token) {
                    $problems.Add("${relative}:${line}  showPage 摆的是裸 $type，认不出 objectName；" +
                        "给它 setObjectName() 并按「$type#objectName」登记，或写「// ${IgnoreMarker} 理由」")
                    continue
                }
                if (-not $registeredInline.Contains($token)) {
                    $problems.Add("${relative}:${line}  showPage 摆的 $token 没登记进 ${RegistryRelative}")
                }
                continue
            }
            if (-not (Test-IsDialogClass $type $baseOf)) {
                $problems.Add("${relative}:${line}  showPage 的第一参数认成了 $type，它不是对话框页面（判据写错？）")
                continue
            }
            if (-not $registeredClasses.Contains($type)) {
                $problems.Add("${relative}:${line}  showPage 摆的页面 $type 没登记进 ${RegistryRelative}")
            }
        }
    }

    # 4. 清单和 sweep 互相对得上：键要真的 record() 过，入口要真的存在。
    $sweepText = [IO.File]::ReadAllText($sweepPath)
    $recorded = @([regex]::Matches($sweepText,
            '(?:sweep\.record|overlayDialog|promptSurface)\(\s*QStringLiteral\("([^"]+)"\)') |
            ForEach-Object { $_.Groups[1].Value -replace '%1', '*' })
    if ($recorded.Count -eq 0) { throw "在 ${SweepRelative} 里没扫到任何表面键，规则失配" }
    $knownClasses = Get-KnownClassNames $Root
    foreach ($row in $registry) {
        if (-not (Test-KeyCovered $row.Key $recorded)) {
            $problems.Add("${RegistryRelative}:$($row.Line)  表面键 $($row.Key) 在 sweep 里没有对应的 record()，登记了不扫")
        }
        foreach ($entry in $row.Entries) {
            if ($entry.Contains('#')) {
                $objectName = $entry.Split('#', 2)[1]
                $quoted = '"' + $objectName + '"'
                $exists = $false
                foreach ($file in Get-SourceFiles $Root) {
                    if ([IO.File]::ReadAllText($file.FullName).Contains($quoted)) { $exists = $true; break }
                }
                if (-not $exists) {
                    $problems.Add("${RegistryRelative}:$($row.Line)  入口 $entry 的 objectName 在 ui/ src/ 里找不到，登记名写错或已改名")
                }
            } elseif (-not $knownClasses.Contains($entry)) {
                $problems.Add("${RegistryRelative}:$($row.Line)  入口 $entry 在 ui/ src/ 里找不到这个类，登记名写错或已删除")
            }
        }
    }

    [pscustomobject]@{
        Problems = $problems.ToArray()
        Surfaces = $registry.Count
        Classes  = $candidates.Count
    }
}

function Write-Problems($result) {
    if ($result.Problems.Count -gt 0) {
        Write-Host ("FAIL 表面登记：{0} 处要处理（清单 {1} 个表面）" -f $result.Problems.Count, $result.Surfaces)
        foreach ($p in $result.Problems) { Write-Host "  $p" }
        return 1
    }
    Write-Host ("PASS 表面登记：{0} 个表面、{1} 个对话框子类，新入口都登记过" -f $result.Surfaces, $result.Classes)
    0
}

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("surface-registry-selftest-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        New-Item -ItemType Directory -Force -Path (Join-Path $tmp 'ui'), (Join-Path $tmp 'src'),
            (Join-Path $tmp 'tests/ui_check/modules') | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp 'ui/GoodPage.hpp') -Value @'
#pragma once
#include <QDialog>
class GoodPage : public QDialog {
};
'@
        Set-Content -LiteralPath (Join-Path $tmp 'ui/StrayDialog.hpp') -Value @'
#pragma once
#include <QDialog>
class StrayDialog : public QDialog {
};
'@
        Set-Content -LiteralPath (Join-Path $tmp 'ui/IgnoredDialog.hpp') -Value @'
#pragma once
#include <QDialog>
class IgnoredDialog : public QDialog {  // surface-registry-ignore: 只是外壳，内容页单独扫
};
'@
        Set-Content -LiteralPath (Join-Path $tmp 'src/Stray.cpp') -Value @'
#include "GoodPage.hpp"
void open(OverlayHost *overlay) {
    overlay->showPage(new StrayDialog, QStringLiteral("x"));
}
'@
        Set-Content -LiteralPath (Join-Path $tmp 'tests/ui_check/modules/UiSurfaceSweepCheck.cpp') -Value @'
void f() {
    sweep.record(QStringLiteral("good"), QStringLiteral("好页"), QStringLiteral(""), QStringLiteral(""), QString(), page);
}
'@
        Set-Content -LiteralPath (Join-Path $tmp 'tests/ui_check/surface-registry.txt') -Value @'
# 自测样本
good | GoodPage | 好页
'@
        $result = Invoke-SurfaceRegistryCheck $tmp
        $fail = 0
        foreach ($needle in @('对话框子类 StrayDialog 没登记', 'showPage 摆的页面 StrayDialog 没登记')) {
            if (-not ($result.Problems | Where-Object { $_ -like "*$needle*" })) {
                Write-Host "自测失败：没报出 $needle"; ++$fail
            }
        }
        foreach ($unwanted in @('GoodPage 没登记', 'IgnoredDialog 没登记')) {
            if ($result.Problems | Where-Object { $_ -like "*$unwanted*" }) {
                Write-Host "自测失败：误报了 $unwanted"; ++$fail
            }
        }
        # 清单里登记一个 sweep 没扫的表面，要红。
        Set-Content -LiteralPath (Join-Path $tmp 'tests/ui_check/surface-registry.txt') -Value @'
good | GoodPage | 好页
ghost | GoodPage | sweep 里没有
'@
        $result2 = Invoke-SurfaceRegistryCheck $tmp
        if (-not ($result2.Problems | Where-Object { $_ -like '*表面键 ghost 在 sweep 里没有*' })) {
            Write-Host '自测失败：没报出「登记了不扫」'; ++$fail
        }
        if ($fail) { exit 1 }
        Write-Host 'PASS 自测：未登记的类、showPage 目标、登记了不扫的表面都报出来了，合法样本与豁免行没有误报'
        exit 0
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not $RepoRoot) { $RepoRoot = Split-Path $PSScriptRoot -Parent }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$result = Invoke-SurfaceRegistryCheck $RepoRoot
exit (Write-Problems $result)
