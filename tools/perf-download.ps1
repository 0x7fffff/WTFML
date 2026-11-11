#Requires -Version 7.0
<#
.SYNOPSIS
    并发下载基准：把 docs/dev/PERFORMANCE.md 第九节那张表原样跑一遍。

.DESCRIPTION
    每个用例起一次 WTFMLPerfDownload.exe：本机 127.0.0.1 起 HTTP/1.1 服务器，
    用**产品同一份** DownloadManager 下一批文件，量完成时间、吞吐、失败数与主线程最长空档。
    每例先热身（默认 1 轮，只留最后一次的覆盖结果），再测 -Rounds 轮，报表取中位。

    这些数字只说明调度 / 写盘 / 主线程影响，**不代表广域网**：
    loopback 没有 RTT，吞吐上限由内存与磁盘决定。

.PARAMETER Exe
    基准 exe。默认 build/perf-download/tools/perf_download/WTFMLPerfDownload.exe
    （编法见 docs/dev/PERFORMANCE.md 第九节：-DWTFML_PERF_DOWNLOAD=ON、Release）。

.PARAMETER ReportDir
    结果落点，默认 build/perf-download-<时间戳>/。每个用例一份 JSON，外加 summary.json。

.PARAMETER Rounds
    每例的测量轮数，默认 3。

.PARAMETER Warmup
    每例的热身轮数，默认 1。

.PARAMETER Only
    只跑名字匹配的用例（通配符），例如 -Only '*seg*'。
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/perf-download/tools/perf_download/WTFMLPerfDownload.exe',
    [string]$ReportDir = '',
    [int]$Rounds = 3,
    [int]$Warmup = 1,
    [string]$Only = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    if (-not (Test-Path -LiteralPath $Exe)) {
        throw "找不到 $Exe —— 先按 docs/dev/PERFORMANCE.md 第九节编一次（-DWTFML_PERF_DOWNLOAD=ON）"
    }
    if (-not $ReportDir) {
        $ReportDir = Join-Path 'build' ('perf-download-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    }
    New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

    # 用例表：name / 文件数 / 单文件大小 / 并发 / 是否允许分段 / 限速（字节/秒）
    #
    # small-*      ：100 MiB 小文件，看并发档位对"每文件一个往返"的摊薄（产品默认 24）。
    # many-*       ：1000 个小文件，看调度与信号量本身的成本。
    # large-*      ：200 MiB、单文件 5 MiB，plain 与 seg 各一跑 —— 5 MiB 已经过
    #                ResumableDownload::kMinimumBytes（4 MiB），seg 那跑才算"分段升级"。
    # limit-*      ：限 4 MiB/s，量的是令牌桶而不是网线（期望：24 MiB ≈ 6 s、31 MiB ≈ 8 s）。
    #                ★ 小文件那一跑是"收尾那一口"的判据：文件比 QNAM 缓冲还小的时候，
    #                整份正文都在结束信号之后才写出去（见 DownloadManager::throttledReadBufferBytes）。
    #                ★ 2x5M 那两跑是"预读高水位按谁分"的判据：只开一两条作业时若还按 24 路
    #                去分，每条只剩几 KB，一拍（100 ms）只读得到那么多，速率会掉到远低于
    #                设定值（实测 1.2 MiB/s 与 4.5 MiB/s 两种写法，修好后 ≈8）。
    $cases = @(
        [pscustomobject]@{ name = 'small-100MiB-c6';        files = 400;  size = '256K'; concurrency = 6;  segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'small-100MiB-c12';       files = 400;  size = '256K'; concurrency = 12; segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'small-100MiB-c24';       files = 400;  size = '256K'; concurrency = 24; segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'small-100MiB-c48';       files = 400;  size = '256K'; concurrency = 48; segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'many-1000x64K-c24';      files = 1000; size = '64K';  concurrency = 24; segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'large-200MiB-plain-c24'; files = 40;   size = '5M';   concurrency = 24; segmented = $false; limit = 0 },
        [pscustomobject]@{ name = 'large-200MiB-seg-c24';   files = 40;   size = '5M';   concurrency = 24; segmented = $true;  limit = 0 },
        [pscustomobject]@{ name = 'limit-4MiBps-24MiB-c24';  files = 24;   size = '1M';   concurrency = 24; segmented = $false; limit = 4MB },
        [pscustomobject]@{ name = 'limit-4MiBps-1000x32K';   files = 1000; size = '32K';  concurrency = 24; segmented = $false; limit = 4MB },
        [pscustomobject]@{ name = 'limit-8MiBps-2x5M-c24';    files = 2;    size = '5M';   concurrency = 24; segmented = $false; limit = 8MB },
        [pscustomobject]@{ name = 'limit-8MiBps-2x5M-seg';    files = 2;    size = '5M';   concurrency = 24; segmented = $true;  limit = 8MB }
    )
    if ($Only) {
        $cases = @($cases | Where-Object { $_.name -like $Only })
        if (-not $cases) { throw "没有用例匹配 -Only '$Only'" }
    }

    $summary = @()
    foreach ($case in $cases) {
        $json = Join-Path $ReportDir ($case.name + '.json')
        $arguments = @(
            '--files', $case.files, '--size', $case.size, '--concurrency', $case.concurrency,
            '--rounds', $Rounds, '--warmup', $Warmup, '--label', $case.name, '--json', $json
        )
        if ($case.segmented) { $arguments += '--segmented' }
        if ($case.limit -gt 0) { $arguments += @('--speed-limit', [string]$case.limit) }

        & $Exe @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "用例 $($case.name) 失败（exit $LASTEXITCODE）：看上面那几行与 $json"
        }
        $report = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
        $summary += [pscustomobject]@{
            case        = $case.name
            files       = $case.files
            size        = $case.size
            concurrency = $case.concurrency
            segmented   = $case.segmented
            limit       = $case.limit
            wall_ms     = [math]::Round($report.median.wall_ms, 1)
            mib_per_s   = [math]::Round($report.median.megabytes_per_second, 1)
            max_stall_ms = [math]::Round($report.median.max_stall_ms, 0)
            failed      = ($report.rounds | Measure-Object -Property failed -Sum).Sum
            verified    = -not ($report.rounds | Where-Object { -not $_.verified })
        }
    }

    $summary | Format-Table -AutoSize
    $summaryPath = Join-Path $ReportDir 'summary.json'
    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    Write-Host "结果：$ReportDir"
}
finally {
    Pop-Location
}
