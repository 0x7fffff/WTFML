#Requires -Version 7.0
<#
.SYNOPSIS
    检查仓库内文档引用是否还指得着：markdown 里的相对链接 + 源码注释里提到的 docs/*.md。

.DESCRIPTION
    两类引用最容易在改名/删文件之后腐坏：

    1. `docs/*.md`、`README.md` 等 markdown 里的 `[文字](相对路径)`；
    2. `src/`、`ui/`、`tests/`、`tools/` 注释里写的 `docs/*.md`。

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

# ---- 1. markdown 里的相对链接 -------------------------------------------------
$mdFiles = @('README.md', 'FEATURES.md', 'CODING_PLAN_FULL.md', 'CONTRIBUTING_COMMENT.md', 'LOCAL_FILES.md', 'Agent.md')
$mdFiles += Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'docs') -Filter '*.md' -File |
    ForEach-Object { 'docs/' + $_.Name }

foreach ($rel in $mdFiles) {
    $file = Join-Path $RepoRoot $rel
    if (-not (Test-Path -LiteralPath $file)) { continue }
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

# ---- 2. 源码注释里的 docs/*.md --------------------------------------------
$codeDirs = @('src', 'ui', 'tests', 'tools')
foreach ($dirName in $codeDirs) {
    $dir = Join-Path $RepoRoot $dirName
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Include *.cpp, *.hpp, *.ps1) {
        if ($f.FullName -match '\\build') { continue }
        $lineNo = 0
        foreach ($line in Get-Content -LiteralPath $f.FullName) {
            $lineNo++
            foreach ($m in [regex]::Matches($line, 'docs/[A-Za-z0-9_\-\.]+\.md\b')) {
                $checkedDocRefs++
                $ref = $m.Value
                if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $ref))) {
                    $missing.Add("$dirName/$($f.Name):$lineNo -> $ref")
                }
            }
        }
    }
}

Write-Host ("markdown 相对链接 {0} 条、源码里的 docs 引用 {1} 条" -f $checkedLinks, $checkedDocRefs)
if ($missing.Count -gt 0) {
    Write-Host "缺失的引用："
    $missing | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host 'PASS 文档引用都能指得着'
exit 0

