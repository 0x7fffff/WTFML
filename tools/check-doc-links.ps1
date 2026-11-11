#Requires -Version 7.0
<#
.SYNOPSIS
    检查仓库内文档引用是否还指得着：markdown 里的相对链接 + 源码注释/脚本里提到的 docs/ 下的 .md（含子目录）。

.DESCRIPTION
    两类引用最容易在改名/移动/删文件之后腐坏：

    1. 仓库里所有 markdown（递归；排除 build*/、.git/、tests/meta 夹具）里的 `[文字](相对路径)`；
    2. `src/`、`ui/`、`tests/`、`tools/` 与根 CMakeLists.txt 里写的 `docs/…/X.md`
       （.cpp/.hpp/.ps1/.py/.cmake/CMakeLists.txt）。

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

# ---- 1. markdown 里的相对链接 -------------------------------------------------
$mdFiles = Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Filter '*.md' |
    ForEach-Object { Get-RepoRel $_.FullName } |
    Where-Object { $_ -notmatch $skip }

foreach ($rel in $mdFiles) {
    $file = Join-Path $RepoRoot $rel
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
$codeFiles = @(foreach ($dirName in @('src', 'ui', 'tests', 'tools')) {
    $dir = Join-Path $RepoRoot $dirName
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    Get-ChildItem -LiteralPath $dir -Recurse -File -Include *.cpp, *.hpp, *.ps1, *.py, *.cmake, CMakeLists.txt
})
$rootCMake = Join-Path $RepoRoot 'CMakeLists.txt'
if (Test-Path -LiteralPath $rootCMake) { $codeFiles += Get-Item -LiteralPath $rootCMake }

foreach ($f in $codeFiles) {
    $rel = Get-RepoRel $f.FullName
    if ($rel -match $skip) { continue }
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $f.FullName) {
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
