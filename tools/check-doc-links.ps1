#Requires -Version 7.0
<#
.SYNOPSIS
    检查仓库内文档引用是否还指得着：markdown 里的相对链接 + 源码注释/脚本里提到的 docs/ 下的 .md（含子目录）。

.DESCRIPTION
    两类引用最容易在改名/移动/删文件之后腐坏：

    1. 仓库里所有 markdown（递归；排除 build*/、.git/、tests/meta 夹具）里的 `[文字](相对路径)`；
    2. `src/`、`ui/`、`tests/`、`tools/` 与根 CMakeLists.txt 里写的 `docs/…/X.md`
       （.cpp/.hpp/.ps1/.py/.cmake/CMakeLists.txt）。

    扫的范围是 **git 跟踪的文件**（`git ls-files`）：被 .gitignore 挡住的草稿（`.tongxi/…`、
    临时改名底稿）不在仓库里，链接写成什么样都不该让这条检查变红。取不到 git（不是仓库、
    没装 git）时退回扫盘，宁可变吵，也不静默放过。

    检查是**只读**的：不改任何文件，只报缺了哪些、谁在引。
    任何一条找不到就退出码 1（可以直接串进例行检查）。

.PARAMETER RepoRoot
    仓库根目录。默认是脚本所在目录的上一级。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) { $RepoRoot = Split-Path $PSScriptRoot -Parent }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$missing = [Collections.Generic.List[string]]::new()
$checkedLinks = 0
$checkedDocRefs = 0

# 相对仓库根的 / 分隔路径；build*/、.git/、tests/meta（测试夹具，不是文档）不扫。
function Get-RepoRel([string]$FullName) {
    $FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')
}
$skip = '^(build[^/]*|\.git|tests/meta)/'

# 仓库里**入库**的那些文件（相对仓库根、/ 分隔）。返回 $null = 这份 git 读不到，调用方退回扫盘。
#
# NOTE: 用 `-z`（NUL 分隔）而不是换行：文件名里带空格、括号、中文都原样过来，不用猜 git 的
# 引号规则。输出按 UTF-8 解 —— 控制台代码页在有的机器上不是 UTF-8，那会把中文路径读成乱码，
# 于是"入库的文件"一个都对不上，检查反倒静默扫了零个文件。
function Get-TrackedRepoFiles {
    $previous = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $raw = & git -C $RepoRoot ls-files -z 2>$null
        $code = $LASTEXITCODE
    } catch {
        return $null
    } finally {
        [Console]::OutputEncoding = $previous
    }
    if ($code -ne 0 -or $null -eq $raw) {
        return $null
    }
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($name in (((@($raw) -join "`n") -split "`0") | ForEach-Object { $_.TrimEnd("`r") })) {
        if ($name) { $null = $set.Add($name) }
    }
    if ($set.Count -eq 0) {
        return $null  # 空仓库 / git 认的根不是这里：当作读不到，退回扫盘
    }
    return ,$set
}

$tracked = Get-TrackedRepoFiles
if ($null -eq $tracked) {
    Write-Host 'git 读不到跟踪清单：退回扫盘（未入库的文件也会被扫到）'
}

# ---- 1. markdown 里的相对链接 -------------------------------------------------
$mdFiles = if ($null -ne $tracked) {
    @($tracked | Where-Object { $_ -like '*.md' } | Where-Object { $_ -notmatch $skip } | Sort-Object)
} else {
    @(Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Filter '*.md' |
        ForEach-Object { Get-RepoRel $_.FullName } |
        Where-Object { $_ -notmatch $skip })
}

foreach ($rel in $mdFiles) {
    $file = Join-Path $RepoRoot $rel
    if (-not (Test-Path -LiteralPath $file)) { continue }  # 索引里有、工作区删了（还没提交的删除）
    $dir = Split-Path $file -Parent
    foreach ($line in Get-Content -LiteralPath $file) {
        foreach ($m in [regex]::Matches($line, '\]\(([^)\s]+)\)')) {
            $target = $m.Groups[1].Value
            if ($target -match '^(https?:|mailto:|#)') { continue }
            $path = ($target -split '#')[0]
            if ([string]::IsNullOrWhiteSpace($path)) { continue }
            $checkedLinks++
            $abs = [IO.Path]::GetFullPath((Join-Path $dir $path.Replace('/', '\')))
            if (-not (Test-Path -LiteralPath $abs)) {
                $missing.Add("$rel -> $target")
            }
        }
    }
}

# ---- 2. 源码/脚本里的 docs/…/X.md ------------------------------------------
# 范围同上面：入库的那些 `src/|ui/|tests/|tools/` 下的源码/脚本，加根 CMakeLists.txt。
$codePattern = '\.(cpp|hpp|ps1|py|cmake)$|(^|/)CMakeLists\.txt$'
$codeFiles = if ($null -ne $tracked) {
    @($tracked | Where-Object {
        ($_ -match '^(src|ui|tests|tools)/' -or $_ -eq 'CMakeLists.txt') -and
        $_ -match $codePattern -and $_ -notmatch $skip
    } | Sort-Object)
} else {
    @(foreach ($dirName in @('src', 'ui', 'tests', 'tools')) {
        $dir = Join-Path $RepoRoot $dirName
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        Get-ChildItem -LiteralPath $dir -Recurse -File -Include *.cpp, *.hpp, *.ps1, *.py, *.cmake, CMakeLists.txt |
            ForEach-Object { Get-RepoRel $_.FullName } |
            Where-Object { $_ -notmatch $skip }
    })
    $rootCMake = Join-Path $RepoRoot 'CMakeLists.txt'
    if (Test-Path -LiteralPath $rootCMake) { 'CMakeLists.txt' }
}

foreach ($rel in $codeFiles) {
    $file = Join-Path $RepoRoot $rel
    if (-not (Test-Path -LiteralPath $file)) { continue }  # 索引里有、工作区删了（还没提交的删除）
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $file) {
        $lineNo++
        foreach ($m in [regex]::Matches($line, 'docs/(?:[A-Za-z0-9_\-]+/)*[A-Za-z0-9_\-\.]+\.md\b')) {
            $checkedDocRefs++
            $ref = $m.Value
            if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $ref))) {
                $missing.Add("${rel}:$lineNo -> $ref")
            }
        }
    }
}

Write-Host ("markdown 文件 {0} 个、相对链接 {1} 条、源码里的 docs 引用 {2} 条" -f @($mdFiles).Count, $checkedLinks, $checkedDocRefs)
if ($missing.Count -gt 0) {
    Write-Host "缺失的引用："
    $missing | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host 'PASS 文档引用都能指得着'
exit 0
