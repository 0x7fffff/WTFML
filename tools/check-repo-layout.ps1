#Requires -Version 7.0
<#
.SYNOPSIS
    检查已入库文件的布局是否守规矩：根目录白名单、docs 子目录规则、日期后缀、禁入库扩展名。

.DESCRIPTION
    规则见仓库根目录 AGENTS.md「目录布局」。用 `git ls-files` 取已跟踪文件，逐条检查：

    1. 根目录文件只许白名单里的（README.md LICENSE AGENTS.md CLAUDE.md GEMINI.md
       CMakeLists.txt CMakePresets.json .clang-format .gitattributes .gitignore）；
    2. 根目录下的目录只许 .github assets docs res src tests tools ui；
    3. docs/ 下的 .md 只能在 docs 顶层白名单（README.md FEATURES.md ROADMAP.md），
       或 docs/features、docs/dev、docs/dev/perf-notes、docs/archive 里；
    4. 文件名带 8 位日期后缀（_20yymmdd）的 .md 只许在 docs/archive/；
    5. 不许入库 *.log *.dmp *.bak *.orig *.rej *.stackdump *.tmp。

    检查是**只读**的：不改任何文件，只列出违规项。有违规就退出码 1。

.PARAMETER RepoRoot
    仓库根目录。默认是脚本所在目录的上一级。

.PARAMETER SelfTest
    不查真实仓库，改在临时目录里 git init 造违规样本，验证上面几条检测逻辑本身。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$RootFiles = 'README.md', 'LICENSE', 'AGENTS.md', 'CLAUDE.md', 'GEMINI.md', 'CMakeLists.txt',
    'CMakePresets.json', '.clang-format', '.gitattributes', '.gitignore'
$RootDirs = '.github', 'assets', 'docs', 'res', 'src', 'tests', 'tools', 'ui'
$DocsTop = 'README.md', 'FEATURES.md', 'ROADMAP.md'
$DocsDirs = 'docs/features', 'docs/dev', 'docs/dev/perf-notes', 'docs/archive'
$BannedExt = '\.(log|dmp|bak|orig|rej|stackdump|tmp)$'

# 入参是相对仓库根、用 / 分隔的路径列表；返回违规说明列表。
function Test-RepoLayout([string[]]$Files) {
    $bad = [Collections.Generic.List[string]]::new()
    foreach ($f in $Files) {
        $parts = $f -split '/'
        if ($parts.Count -eq 1) {
            if ($f -notin $RootFiles) { $bad.Add("$f  根目录不许放这个文件（白名单见 AGENTS.md）") }
        } elseif ($parts[0] -notin $RootDirs) {
            $bad.Add("$f  根目录不许有 '$($parts[0])/' 目录")
        }
        if ($f -match $BannedExt) { $bad.Add("$f  禁止入库的扩展名") }
        if ($f -match '\.md$') {
            $dir = if ($parts.Count -gt 1) { $parts[0..($parts.Count - 2)] -join '/' } else { '' }
            if ($f -like 'docs/*') {
                if ($dir -eq 'docs') {
                    if ($parts[-1] -notin $DocsTop) { $bad.Add("$f  docs 顶层只许 README.md FEATURES.md ROADMAP.md") }
                } elseif ($dir -notin $DocsDirs) {
                    $bad.Add("$f  docs 下的 .md 只能放 features/ dev/ dev/perf-notes/ archive/")
                }
            }
            if ($parts[-1] -match '_20\d{6}' -and $dir -ne 'docs/archive') {
                $bad.Add("$f  带日期后缀的 .md 只许在 docs/archive/")
            }
        }
    }
    , $bad
}

function Get-TrackedFiles([string]$Root) {
    $out = git -C $Root -c core.quotepath=off ls-files
    if ($LASTEXITCODE -ne 0) { throw "git ls-files 失败：$Root 不是 git 仓库？" }
    @($out | Where-Object { $_ })
}

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("layout-selftest-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        git -C $tmp init -q
        # 合法样本 + 每条规则各一个违规样本。docs 前缀用变量拼，免得 check-doc-links 把样本路径当真实引用。
        $d = 'docs'
        $good = 'README.md', 'AGENTS.md', 'src/a.cpp', "$d/README.md", "$d/features/X.md",
            "$d/dev/perf-notes/Y.md", "$d/archive/Z_20261105.md", "$d/archive/LOG.md", '.github/workflows/ci.yml'
        $badCases = [ordered]@{
            'NOTES.md'                    = '根目录不许放'
            'junk/a.txt'                  = '根目录不许有'
            "$d/EXTRA.md"                 = 'docs 顶层'
            "$d/misc/a.md"                = 'docs 下的 .md'
            "$d/dev/perf-notes/q/a.md"    = 'docs 下的 .md'
            "$d/dev/REPORT_20261110.md"   = '日期后缀'
            'src/notes_20261110.md'       = '日期后缀'
            'tools/run.log'               = '禁止入库'
            'crash.dmp'                   = '禁止入库'
        }
        foreach ($p in $good + $badCases.Keys) {
            $full = Join-Path $tmp $p
            New-Item -ItemType Directory -Force -Path (Split-Path $full -Parent) | Out-Null
            Set-Content -LiteralPath $full -Value 'x'
        }
        git -C $tmp add -A
        $found = Test-RepoLayout (Get-TrackedFiles $tmp)
        $fail = 0
        foreach ($g in $good) {
            if ($found | Where-Object { $_ -like "$g  *" }) { Write-Host "自测失败：合法文件被误报 $g"; $fail++ }
        }
        foreach ($k in $badCases.Keys) {
            if (-not ($found | Where-Object { $_ -like "$k  *" -and $_ -like "*$($badCases[$k])*" })) {
                Write-Host "自测失败：违规样本没被报出 $k（$($badCases[$k])）"; $fail++
            }
        }
        if ($fail) { exit 1 }
        Write-Host ("PASS 自测：{0} 个合法样本无误报，{1} 个违规样本全部报出" -f $good.Count, $badCases.Count)
        exit 0
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not $RepoRoot) { $RepoRoot = Split-Path $PSScriptRoot -Parent }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$files = Get-TrackedFiles $RepoRoot
$bad = Test-RepoLayout $files

Write-Host ("已入库文件 {0} 个" -f $files.Count)
if ($bad.Count -gt 0) {
    Write-Host '布局违规（规则见 AGENTS.md）：'
    $bad | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host 'PASS 仓库布局符合规则'
exit 0
