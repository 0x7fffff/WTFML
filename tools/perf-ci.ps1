#Requires -Version 7.0
<#
.SYNOPSIS
    读 GitHub Actions 最近若干次运行的作业/步骤耗时（只读，不触发任何运行）。

.DESCRIPTION
    P2「测量＋优化｜构建与运行」的 CI 一半：把「缓存/分片到底省了多少、哪个 suite
    是长尾」变成数字。只调 GET：
      /repos/{repo}/actions/runs
      /repos/{repo}/actions/runs/{id}/jobs
    凭据用 git 已经存好的那一份（`git credential fill`），**token 一个字都不打印**。

    分片就是 CI 里 matrix.suite 那几份作业；缓存是 sccache（CI 里 `sccache --show-stats`
    打在步骤日志里，API 读不到计数，所以这里只给各步骤墙钟）。

    连不上就报错退出，不重试（外网失败等用户开代理）。

.EXAMPLE
    pwsh -File tools/perf-ci.ps1 -Limit 8
#>
[CmdletBinding()]
param(
    [string]$Repo = '0x7fffff/WTFML',
    [int]$Limit = 8,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$LASTEXITCODE = 0

$repoRoot = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $OutDir) { $OutDir = Join-Path $repoRoot "build/perf-ci-$stamp" }
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $repoRoot $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cred = "protocol=https`nhost=github.com`n`n" | & git credential fill 2>$null
$token = (@($cred) | Where-Object { $_ -like 'password=*' } | Select-Object -First 1)
if (-not $token) { throw 'git 里没有 github.com 的凭据；先 gh auth login 或 git push 一次。' }
$token = $token.Substring('password='.Length)
if (-not $token) { throw 'git 凭据里没有可用的 token。' }

$headers = @{
    'Authorization' = "Bearer $token"
    'Accept'        = 'application/vnd.github+json'
    'User-Agent'    = 'wtfml-perf-ci'
}

function Get-Json([string]$Uri) {
    try {
        return Invoke-RestMethod -Uri $Uri -Headers $headers -Method Get -TimeoutSec 30
    } catch {
        $msg = $_.Exception.Message
        throw "请求失败：$Uri`n$msg`n（连不上就停手，不重试。）"
    }
}

$workflowRuns = Get-Json "https://api.github.com/repos/$Repo/actions/runs?per_page=$Limit"
$runList = @($workflowRuns.workflow_runs | Where-Object { $_.status -eq 'completed' })
if ($runList.Count -eq 0) { throw '最近没有已完成的运行。' }

function Get-Seconds([string]$From, [string]$To) {
    if (-not $From -or -not $To) { return $null }
    return [math]::Round(([datetimeoffset]::Parse($To) - [datetimeoffset]::Parse($From)).TotalSeconds, 1)
}

$runs = [Collections.Generic.List[object]]::new()
foreach ($run in $runList) {
    $jobsResponse = Get-Json "https://api.github.com/repos/$Repo/actions/runs/$($run.id)/jobs?per_page=100"
    $jobs = @()
    foreach ($job in @($jobsResponse.jobs)) {
        $steps = @()
        foreach ($step in @($job.steps)) {
            $steps += [pscustomobject][ordered]@{
                name    = $step.name
                number  = $step.number
                status  = $step.status
                conclusion = $step.conclusion
                seconds = Get-Seconds $step.started_at $step.completed_at
            }
        }
        $jobs += [pscustomobject][ordered]@{
            name       = $job.name
            conclusion = $job.conclusion
            seconds    = Get-Seconds $job.started_at $job.completed_at
            runnerName = $job.runner_name
            steps      = $steps
        }
    }
    $runs.Add([pscustomobject][ordered]@{
        id         = $run.id
        name       = $run.name
        event      = $run.event
        attempt    = $run.run_attempt
        conclusion = $run.conclusion
        headSha    = $run.head_sha
        createdAt  = $run.created_at
        seconds    = Get-Seconds $run.created_at $run.updated_at
        jobs       = $jobs
    })
    Write-Host ("  运行 {0}：{1}（{2}）{3:N0}s" -f $run.run_attempt, $run.name, $run.conclusion, (Get-Seconds $run.created_at $run.updated_at))
}

function Get-Median([object[]]$Values) {
    $v = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
    if ($v.Count -eq 0) { return $null }
    if ($v.Count % 2 -eq 1) { return $v[[int](($v.Count - 1) / 2)] }
    return [math]::Round(($v[$v.Count / 2 - 1] + $v[$v.Count / 2]) / 2, 1)
}

# 按作业名聚合（matrix.suite 的每一份就是一条），再按步骤名聚合长尾。
$byJob = [ordered]@{}
$byStep = [ordered]@{}
foreach ($run in $runs) {
    foreach ($job in $run.jobs) {
        if (-not $byJob.Contains($job.name)) { $byJob[$job.name] = [Collections.Generic.List[object]]::new() }
        $byJob[$job.name].Add($job.seconds)
        foreach ($step in $job.steps) {
            $key = "$($job.name) :: $($step.name)"
            if (-not $byStep.Contains($key)) { $byStep[$key] = [Collections.Generic.List[object]]::new() }
            $byStep[$key].Add($step.seconds)
        }
    }
}

$jobStats = [ordered]@{}
foreach ($name in $byJob.Keys) {
    $jobStats[$name] = [ordered]@{
        n      = $byJob[$name].Count
        median = Get-Median $byJob[$name]
        min    = (@($byJob[$name] | Where-Object { $null -ne $_ } | Sort-Object) | Select-Object -First 1)
        max    = (@($byJob[$name] | Where-Object { $null -ne $_ } | Sort-Object) | Select-Object -Last 1)
    }
}
$stepStats = [ordered]@{}
foreach ($name in $byStep.Keys) {
    $stepStats[$name] = [ordered]@{
        n      = $byStep[$name].Count
        median = Get-Median $byStep[$name]
        max    = (@($byStep[$name] | Where-Object { $null -ne $_ } | Sort-Object) | Select-Object -Last 1)
    }
}

$report = [ordered]@{
    schema = 1
    stamp  = $stamp
    repo   = $Repo
    limit  = $Limit
    runs   = $runs
    jobs   = $jobStats
    steps  = $stepStats
    notes  = @('sccache 命中数在步骤日志里，API 读取不到；这里只有墙钟。')
}
$jsonPath = Join-Path $OutDir 'ci.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$lines = [Collections.Generic.List[string]]::new()
$lines.Add("# CI 作业耗时 $stamp")
$lines.Add('')
$lines.Add("- 仓库：$Repo；已完成的最近 $($runs.Count) 次运行（$($runs[0].createdAt) 起）")
$lines.Add('')
$lines.Add('## 作业（分片）')
$lines.Add('')
$lines.Add('| 作业 | 次数 | 中位秒 | 最短 | 最长 |')
$lines.Add('| --- | ---: | ---: | ---: | ---: |')
foreach ($name in $jobStats.Keys) {
    $s = $jobStats[$name]
    $lines.Add(("| {0} | {1} | {2} | {3} | {4} |" -f $name, $s.n, $s.median, $s.min, $s.max))
}
$lines.Add('')
$lines.Add('## 最长的 12 个步骤（按中位）')
$lines.Add('')
$lines.Add('| 步骤 | 次数 | 中位秒 | 最长 |')
$lines.Add('| --- | ---: | ---: | ---: |')
$slowest = @($stepStats.Keys | Sort-Object { $stepStats[$_].median } -Descending | Select-Object -First 12)
foreach ($name in $slowest) {
    $s = $stepStats[$name]
    $lines.Add(("| {0} | {1} | {2} | {3} |" -f $name, $s.n, $s.median, $s.max))
}
$summaryPath = Join-Path $OutDir 'summary.md'
$lines | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host ''
Write-Host "报告：$jsonPath"
Write-Host "摘要：$summaryPath"
