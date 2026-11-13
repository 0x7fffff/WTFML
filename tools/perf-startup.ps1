#Requires -Version 7.0
<#
.SYNOPSIS
    真·产品 exe 的启动/就绪耗时与界面卡顿测量（外部测量，不改产品代码）。

.DESCRIPTION
    P2「测量＋优化｜构建与运行」的第二半。测的是 **Release（-O3）** 编出来的
    WTFMinecraftLauncher.exe —— 验证工程（mw_check/verify）按设计是 -O0，量出来的
    速度不能代表产品（docs/dev/TESTING.md 的性能规则）。

    为什么要换注册表：产品的"数据目录"是 QSettings 里的 Launcher/CurrentFolder
    （HKCU\Software\WTFML\WTFML）。不换就会写到用户真正在用的游戏文件夹里。这里把
    整个 HKCU\Software\WTFML 导出成 backup.reg → 删掉 → 种一份指向沙盒的最小设置 →
    跑完在 finally 里删干净、再 import 回来，并再导出一份和备份逐字节比对（SHA256）。
    比对不一致会写 registry-diff.txt 并以失败退出。

    # settings-isolation: real-settings —— 冷启动量的就是"真实设置在这一台机器上从零读起"，
    用 --acceptance-profile 换掉的是被量的东西本身（换成了 INI 与临时数据根）。所以它必须
    动真 HKCU；代价由上面那段兜着：跑前导出备份、跑后 import 回来并逐字节比对，中断时按
    报告目录里的 restore.ps1 还原。

    ⚠ 跑之前必须没有启动器在运行：脚本会检查，发现就拒绝（不会去杀用户那个）。
    ⚠ 如果脚本被强行中断，用报告目录里的 restore.ps1 手工还原。

    指标（每次运行一条）：
      visibleMs  子进程创建 → 找到标题为 "WTF Minecraft Launcher" 的可见顶层窗口
      readyMs    → 该窗口开始应答 WM_NULL（消息循环已经转起来，可以点）
      maxBlockMs 窗口出现后 StallSeconds 秒内，WM_NULL 往返的最长一次（主线程最长阻塞）
      slowPings  这段时间里超过 500 ms 没应答的次数（卡死/长任务）
      内存/CPU   settle 之后的 WorkingSet / PrivateMemorySet / CPU 时间

.PARAMETER Exe
    产品 exe。默认 build/verification-mingw13/WTFMinecraftLauncher.exe（Release）。

.PARAMETER Runs
    计入统计的运行次数，默认 6。

.PARAMETER Instances
    沙盒里预置的实例个数（versions/<id>/<id>.jar + <id>.json），默认 0。

.PARAMETER InstanceMeta
    给每一份预置实例多写一个 `wtfml.meta`（schemaVersion 1、显示名、稳定 instanceId）。
    默认的夹具**没有**这个文件：实例目录刚下载完时本来就没有它，只有改过名 / 归过组 /
    换过图标的实例才会写出来。要量"读元数据那几条路"的时候才打开这一档 ——
    两种夹具的数字不可直接比较。

.PARAMETER StallSeconds
    窗口出现后继续观察界面卡顿的秒数，默认 3。

.EXAMPLE
    pwsh -File tools/perf-startup.ps1 -Runs 6 -Instances 0
    pwsh -File tools/perf-startup.ps1 -Runs 6 -Instances 40
#>
[CmdletBinding()]
param(
    [string]$Exe = '',
    [int]$Runs = 6,
    [int]$Instances = 0,
    [switch]$InstanceMeta,
    [int]$StallSeconds = 3,
    [int]$Warmup = 1,
    # 界面探针模式：给 exe 传 `--wtfml-perf-probe=<沙盒>/perf-probe.json`，由它自己
    # 按真实入口发动作（翻页 / 换主题）、自己退出。要求 exe 是
    # `-DWTFML_PERF_PROBE=ON` 的 Release 构建（见 docs/dev/PERFORMANCE.md）。
    # 这一档不做外部 WM_NULL 探测（会干扰探针自己量的节拍）。
    [switch]$Probe,
    [string]$Sandbox,
    [string]$ReportDir,
    [string]$QtBin = 'D:/Qt/Qt/6.10.1/mingw_64',
    [switch]$KeepSandbox
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$LASTEXITCODE = 0

$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'build/verification-mingw13/WTFMinecraftLauncher.exe' }
$Exe = [IO.Path]::GetFullPath($Exe)
if (-not (Test-Path -LiteralPath $Exe)) { throw "找不到产品 exe：$Exe" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $ReportDir) { $ReportDir = Join-Path $repo "build/perf-runtime-$stamp" }
if (-not [IO.Path]::IsPathRooted($ReportDir)) { $ReportDir = Join-Path $repo $ReportDir }
$ReportDir = [IO.Path]::GetFullPath($ReportDir)
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

if (-not $Sandbox) { $Sandbox = Join-Path $ReportDir 'sandbox' }
if (-not [IO.Path]::IsPathRooted($Sandbox)) { $Sandbox = Join-Path $repo $Sandbox }
$Sandbox = [IO.Path]::GetFullPath($Sandbox)
if (Test-Path -LiteralPath $Sandbox) { Remove-Item -LiteralPath $Sandbox -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Sandbox | Out-Null

# ---- 运行前的闸：用户自己的启动器在跑就拒绝；绝不替用户杀掉它。 ----
$running = @(Get-Process -Name 'WTFMinecraftLauncher' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "检测到启动器正在运行（PID $($running.Id -join ', ')）。先关掉再测：要换注册表，不能和你正在用的那份抢设置。"
}

$env:PATH = (@((Join-Path $QtBin 'bin'), $env:PATH) -join ';')
$env:QT_PLUGIN_PATH = Join-Path $QtBin 'plugins'

# ---- 窗口枚举 / 应答探测 ----
if (-not ('WtfmlPerfWin' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WtfmlPerfWin
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr hWnd, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessageTimeoutW(
        IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr w, IntPtr l);

    private const uint WM_NULL = 0x0000;
    private const uint WM_CLOSE = 0x0010;
    private const uint SMTO_ABORTIFHUNG = 0x0002;

    public static IntPtr FindTitled(uint pid, string title)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) =>
        {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner != pid || !IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512);
            GetWindowTextW(h, sb, sb.Capacity);
            if (sb.ToString() == title) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    /// <summary>WM_NULL 往返；返回 false = 在 timeoutMs 内没有应答（主线程被什么占住了）。</summary>
    public static bool Ping(IntPtr hWnd, uint timeoutMs)
    {
        IntPtr result;
        return SendMessageTimeoutW(hWnd, WM_NULL, IntPtr.Zero, IntPtr.Zero,
            SMTO_ABORTIFHUNG, timeoutMs, out result) != IntPtr.Zero;
    }

    public static void CloseAll(uint pid)
    {
        EnumWindows((h, l) =>
        {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner == pid && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
    }
}
'@
}

# ---- 沙盒夹具 ----
function Write-EmptyZip([string]$Path) {
    # 空 zip 的 EOCD 记录（PK\x05\x06 + 18 个 0）：`InstanceFiles` 认的是"有个 jar 文件"，
    # 这里给一份**格式合法**的空包，免得以后判据变严时夹具先失效。
    [byte[]]$bytes = 0x50,0x4B,0x05,0x06,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    [IO.File]::WriteAllBytes($Path, $bytes)
}

$versionsDir = Join-Path $Sandbox 'versions'
New-Item -ItemType Directory -Force -Path $versionsDir | Out-Null
for ($i = 1; $i -le $Instances; $i++) {
    $id = 'perf-instance-{0:D3}' -f $i
    $dir = Join-Path $versionsDir $id
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $meta = [ordered]@{
        id = $id
        name = "Perf $i"
        type = 'release'
        mainClass = 'net.minecraft.client.main.Main'
        libraries = @()
        time = '2026-01-01T00:00:00+00:00'
    } | ConvertTo-Json -Depth 4
    Set-Content -LiteralPath (Join-Path $dir "$id.json") -Value $meta -Encoding utf8
    Write-EmptyZip (Join-Path $dir "$id.jar")
    if ($InstanceMeta) {
        # 和真实写出来的形状一样：`InstanceMetaFile::mergeWrite()` 成功时一定带 schemaVersion。
        # instanceId 用可复现的固定 UUID（第 4/8 段的首字符定成 4/8，就是合法 UUID 的形状）。
        $metaFile = [ordered]@{
            schemaVersion = 1
            name = "Perf $i"
            instanceId = ('00000000-0000-4000-8000-{0:D12}' -f $i)
        } | ConvertTo-Json -Depth 4
        Set-Content -LiteralPath (Join-Path $dir 'wtfml.meta') -Value $metaFile -Encoding utf8
    }
}

# ---- 注册表换沙盒 ----
$regRoot = 'HKCU\Software\WTFML'
$hadKey = Test-Path 'Registry::HKEY_CURRENT_USER\Software\WTFML'
$backup = Join-Path $ReportDir 'registry-backup.reg'
$backupHash = ''
if ($hadKey) {
    & reg export $regRoot $backup /y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "导出注册表失败（$regRoot）。不做任何改动。" }
    $backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
}

$restoreScript = Join-Path $ReportDir 'restore.ps1'
@"
# 如果 perf-startup.ps1 被强行中断，用这个把启动器设置逐字还原。
# 由 tools/perf-startup.ps1 生成（$stamp）。
reg delete "$regRoot" /f
$(if ($hadKey) { "reg import `"$backup`"" } else { '# 跑之前本来就没有这个键，删掉就算还原。' })
"@ | Set-Content -LiteralPath $restoreScript -Encoding utf8

$exeInfo = Get-Item -LiteralPath $Exe
$exeMd5 = (Get-FileHash -LiteralPath $Exe -Algorithm MD5).Hash.ToLowerInvariant()

# NOTE: 变量名**不能**叫 $runs —— PowerShell 变量不区分大小写，会把上面的
# [int]$Runs 参数整个盖掉，后面 `$i -le $Runs` 就变成"Int32 转 List"的绑定错误。
$runResults = [Collections.Generic.List[object]]::new()
$notes = [Collections.Generic.List[string]]::new()
$current = $null
$restoredVerbatim = $false
$afterExport = Join-Path $ReportDir 'registry-after-restore.reg'

function Invoke-OneRun {
    param([int]$Index, [bool]$Measure)
    $probeOut = if ($Probe) { Join-Path $Sandbox 'perf-probe.json' } else { $null }
    if ($probeOut -and (Test-Path -LiteralPath $probeOut)) { Remove-Item -LiteralPath $probeOut -Force }
    $proc = if ($Probe) {
        # 沙盒路径里可能有空格，参数整体带上引号再交给 Start-Process。
        Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) `
            -ArgumentList "--wtfml-perf-probe=`"$probeOut`"" -PassThru
    } else {
        Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) -PassThru
    }
    $script:current = $proc
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $hwnd = [IntPtr]::Zero
    $exitedEarly = $false
    while ($sw.Elapsed.TotalMilliseconds -lt 60000) {
        if ($proc.HasExited) { $exitedEarly = $true; break }
        $hwnd = [WtfmlPerfWin]::FindTitled([uint32]$proc.Id, 'WTF Minecraft Launcher')
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 10
    }
    $visibleMs = $sw.Elapsed.TotalMilliseconds
    if ($hwnd -eq [IntPtr]::Zero) {
        $code = if ($exitedEarly) { $proc.ExitCode } else { $null }
        # 没等到主窗口：把看到的窗口标题全记下来，方便判断是不是弹了对话框。
        $titles = @()
        if (-not $exitedEarly) {
            $titles = @(Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Select-Object -ExpandProperty MainWindowTitle)
        }
        if (-not $exitedEarly) {
            [WtfmlPerfWin]::CloseAll([uint32]$proc.Id)
            if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        }
        throw ("第 {0} 次运行没等到主窗口（60 s）：exited={1} code={2} 标题={3}" -f $Index, $exitedEarly, $code, ($titles -join ' / '))
    }

    if ($Probe) {
        # 探针自己按脚本跑完就 quit()；这里只等它退出、把结果收回来。
        $ok = $proc.WaitForExit(120000)
        if (-not $ok) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        $exitCode = if ($ok) { $proc.Refresh(); $proc.ExitCode } else { $null }
        $script:current = $null
        Start-Sleep -Milliseconds 300

        $appLog = Join-Path $Sandbox 'logs/wtfml.log'
        $logCopy = $null
        if (Test-Path -LiteralPath $appLog) {
            $logCopy = Join-Path $ReportDir ("run-{0:D2}-wtfml.log" -f $Index)
            Copy-Item -LiteralPath $appLog -Destination $logCopy -Force
        }
        $probeCopy = $null
        if ($probeOut -and (Test-Path -LiteralPath $probeOut)) {
            $probeCopy = Join-Path $ReportDir ("run-{0:D2}-probe.json" -f $Index)
            Copy-Item -LiteralPath $probeOut -Destination $probeCopy -Force
        }
        if ($Measure) {
            $runResults.Add([pscustomobject][ordered]@{
                index     = $Index
                visibleMs = [math]::Round($visibleMs, 1)
                cleanExit = $ok -and $exitCode -eq 0
                exitCode  = $exitCode
                probeFile = if ($probeCopy) { [IO.Path]::GetRelativePath($repo, $probeCopy) } else { $null }
                appLog    = if ($logCopy) { [IO.Path]::GetRelativePath($repo, $logCopy) } else { $null }
            })
            Write-Host ("    probe {0}: visible {1:N0} ms / 探针退出={2} / 结果={3}" -f `
                    $Index, $visibleMs, $ok, $(if ($probeCopy) { '有' } else { '没有' }))
        }
        if (-not $ok) { throw ("第 {0} 次探针运行没有在 120 s 内退出。" -f $Index) }
        if ($exitCode -ne 0) { throw ("第 {0} 次探针异常退出：exitCode={1}。日志保留在报告目录。" -f $Index, $exitCode) }
        if (-not $probeCopy) { throw ("第 {0} 次探针没有产出 perf-probe.json（exe 是不是没带 WTFML_PERF_PROBE=ON？）。" -f $Index) }
        Start-Sleep -Milliseconds 300
        return $probeCopy
    }

    # 窗口已可见 → 等它开始应答消息
    $readySw = [Diagnostics.Stopwatch]::StartNew()
    while ($readySw.Elapsed.TotalMilliseconds -lt 15000) {
        if ([WtfmlPerfWin]::Ping($hwnd, 250)) { break }
        Start-Sleep -Milliseconds 10
    }
    $readyMs = $sw.Elapsed.TotalMilliseconds

    $maxBlock = 0.0
    $slow = 0
    $deadline = [DateTime]::UtcNow.AddSeconds($StallSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $t = [Diagnostics.Stopwatch]::StartNew()
        $ok = [WtfmlPerfWin]::Ping($hwnd, 500)
        $t.Stop()
        $ms = $t.Elapsed.TotalMilliseconds
        if (-not $ok) { $slow++ } elseif ($ms -gt $maxBlock) { $maxBlock = $ms }
        Start-Sleep -Milliseconds 20
    }

    Start-Sleep -Milliseconds 300
    $proc.Refresh()
    $wsMb = if ($proc.HasExited) { $null } else { [math]::Round($proc.WorkingSet64 / 1MB, 1) }
    $privMb = if ($proc.HasExited) { $null } else { [math]::Round($proc.PrivateMemorySize64 / 1MB, 1) }
    $cpu = if ($proc.HasExited) { $null } else { [math]::Round($proc.TotalProcessorTime.TotalSeconds, 3) }

    $closed = $proc.CloseMainWindow()
    if (-not $closed) { [WtfmlPerfWin]::CloseAll([uint32]$proc.Id) }
    $clean = $proc.WaitForExit(15000)
    if (-not $clean) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; $clean = $false }
    $script:current = $null
    Start-Sleep -Milliseconds 300

    $appLog = Join-Path $Sandbox 'logs/wtfml.log'
    $logCopy = $null
    if (Test-Path -LiteralPath $appLog) {
        $logCopy = Join-Path $ReportDir ("run-{0:D2}-wtfml.log" -f $Index)
        Copy-Item -LiteralPath $appLog -Destination $logCopy -Force
    }

    if ($Measure) {
        $runResults.Add([pscustomobject][ordered]@{
            index        = $Index
            visibleMs    = [math]::Round($visibleMs, 1)
            readyMs      = [math]::Round($readyMs, 1)
            maxBlockMs   = [math]::Round($maxBlock, 1)
            slowPings    = $slow
            workingSetMb = $wsMb
            privateMb    = $privMb
            cpuSeconds   = $cpu
            cleanExit    = $clean
            appLog       = if ($logCopy) { [IO.Path]::GetRelativePath($repo, $logCopy) } else { $null }
        })
        Write-Host ("    run {0}: visible {1:N0} ms / ready {2:N0} ms / maxBlock {3:N1} ms / slow {4} / WS {5} MB" -f `
                $Index, $visibleMs, $readyMs, $maxBlock, $slow, $wsMb)
    } else {
        Write-Host ("    warmup: visible {0:N0} ms（不计入统计）" -f $visibleMs)
    }
    return $logCopy
}

$failure = $null
try {
    & reg delete $regRoot /f 2>$null | Out-Null
    $launcherKey = "$regRoot\WTFML\Launcher"
    & reg add $launcherKey /v CurrentFolder /t REG_SZ /d $Sandbox /f | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "写 CurrentFolder 失败。" }
    & reg add $launcherKey /v Folders /t REG_MULTI_SZ /d $Sandbox /f | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "写 Folders 失败。" }

    for ($i = 1; $i -le $Warmup; $i++) { Invoke-OneRun -Index 0 -Measure $false | Out-Null }
    for ($i = 1; $i -le $Runs; $i++) { Invoke-OneRun -Index $i -Measure $true | Out-Null }
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($script:current -and -not $script:current.HasExited) {
        Stop-Process -Id $script:current.Id -Force -ErrorAction SilentlyContinue
    }
    # 兜住任何还挂着这个 exe 的进程（只可能是本脚本起的）。.Path 在已退出的进程上会抛，
    # 所以每个都单独包一层。
    Get-Process -Name 'WTFMinecraftLauncher' -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if ($_.Path -eq $Exe) { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
        } catch { }
    }

    & reg delete $regRoot /f 2>$null | Out-Null
    if ($hadKey) {
        & reg import $backup 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Start-Sleep -Milliseconds 400
            & reg import $backup 2>$null | Out-Null
        }
    }
    if ($hadKey) {
        & reg export $regRoot $afterExport /y 2>$null | Out-Null
        $restoredVerbatim = (Test-Path -LiteralPath $afterExport) -and
            ((Get-FileHash -LiteralPath $afterExport -Algorithm SHA256).Hash -eq $backupHash)
        if (-not $restoredVerbatim) {
            $diffPath = Join-Path $ReportDir 'registry-diff.txt'
            try {
                Compare-Object (Get-Content -LiteralPath $backup) (Get-Content -LiteralPath $afterExport) |
                    Format-Table -AutoSize | Out-String | Set-Content -LiteralPath $diffPath -Encoding utf8
            } catch { }
            $notes.Add("注册表还原后与备份不一致，见 $diffPath；可用 restore.ps1 再试一次。")
        }
    } else {
        $restoredVerbatim = -not (Test-Path 'Registry::HKEY_CURRENT_USER\Software\WTFML')
    }
}

if (-not $KeepSandbox) {
    # 沙盒里只有夹具和这次跑出来的日志副本；日志已经另存，留着目录只会占盘。
    # 但注册表还原的证据不能丢，所以报告目录本身永远保留。
    Remove-Item -LiteralPath $Sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

function Get-Stats([string]$Field) {
    # 探针模式那几条记录里没有 readyMs/maxBlockMs 这些列，所以按属性名取、不硬点。
    $values = @($runResults | ForEach-Object {
            $property = $_.PSObject.Properties[$Field]
            if ($property) { $property.Value }
        } | Where-Object { $null -ne $_ })
    if ($values.Count -eq 0) { return $null }
    $sorted = @($values | Sort-Object)
    $median = if ($sorted.Count % 2 -eq 1) { $sorted[[int](($sorted.Count - 1) / 2)] }
              else { ($sorted[$sorted.Count / 2 - 1] + $sorted[$sorted.Count / 2]) / 2 }
    [ordered]@{
        count  = $sorted.Count
        median = [math]::Round($median, 1)
        min    = [math]::Round($sorted[0], 1)
        max    = [math]::Round($sorted[-1], 1)
    }
}

$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$os = (Get-CimInstance Win32_OperatingSystem).Caption
$gitCommit = (& git -C $repo rev-parse --short HEAD 2>$null)
$gitDirty = [bool](& git -C $repo status --porcelain 2>$null)
if ($gitDirty) { $notes.Add('工作区有未提交改动：数字只对应当前工作区。') }

if ($failure) { $notes.Add("运行失败：$failure") }

$report = [ordered]@{
    schema            = 1
    stamp             = $stamp
    kind              = if ($Probe) { 'ui-probe' } else { 'startup' }
    host              = $env:COMPUTERNAME
    os                = $os
    cpu               = $cpu
    ramGb             = $ramGb
    commit            = $gitCommit
    worktreeDirty     = $gitDirty
    exe               = [IO.Path]::GetRelativePath($repo, $Exe)
    exeMd5            = $exeMd5
    exeBytes          = $exeInfo.Length
    exeMtime          = $exeInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
    buildType         = 'Release'
    probe             = [bool]$Probe
    warmupRuns        = $Warmup
    instances         = $Instances
    instanceMeta      = [bool]$InstanceMeta
    sandbox           = [IO.Path]::GetRelativePath($repo, $Sandbox)
    measurement       = if ($Probe) {
        '产品内探针（WTFML_PERF_PROBE=1，Release）：Java/导航/主题、200条合成任务、1000条预设视图、临时预设元数据同步/后台读取；每步量动作+整窗repaint；20ms看门狗标记卡顿跨度。造数及故意同步读取不代表产品界面路径。'
    } else {
        '外部测量：进程创建 → 标题为 "WTF Minecraft Launcher" 的顶层窗口可见 → 应答 WM_NULL；窗口出现后观察主线程最长阻塞。'
    }
    registry          = [ordered]@{
        hadKey              = $hadKey
        backedUp            = [bool]$hadKey
        backupFile          = if ($hadKey) { [IO.Path]::GetRelativePath($repo, $backup) } else { $null }
        restoredVerbatim    = $restoredVerbatim
        restoreScript       = [IO.Path]::GetRelativePath($repo, $restoreScript)
    }
    runs              = $runResults
    stats             = [ordered]@{
        visibleMs  = Get-Stats 'visibleMs'
        readyMs    = Get-Stats 'readyMs'
        maxBlockMs = Get-Stats 'maxBlockMs'
        workingSetMb = Get-Stats 'workingSetMb'
        privateMb  = Get-Stats 'privateMb'
        cpuSeconds = Get-Stats 'cpuSeconds'
    }
    notes             = $notes
}
$jsonPath = Join-Path $ReportDir 'startup.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$s = $report.stats
$lines = [Collections.Generic.List[string]]::new()
$lines.Add("# 启动/就绪基线 $stamp")
$lines.Add('')
$lines.Add("- 机器：$cpu / ${ramGb} GB / $os")
$lines.Add("- exe：$($report.exe)（$($report.buildType)，md5 $exeMd5）")
$lines.Add("- 实例数：$Instances$(if ($InstanceMeta) { '（每个带 wtfml.meta）' })；计入统计：$($runResults.Count) 次（热身 $Warmup 次不计）")
$lines.Add("- 测量：$($report.measurement)")
$lines.Add('')
if ($s.visibleMs) {
    foreach ($name in 'visibleMs', 'readyMs', 'maxBlockMs', 'workingSetMb', 'privateMb', 'cpuSeconds') {
        $st = $s.$name
        if ($st) { $lines.Add("- ${name}：中位 $($st.median)，范围 $($st.min) – $($st.max)（n=$($st.count)）") }
    }
} else {
    $lines.Add('- 没有有效运行。')
}
if ($Probe) {
    $probeJsons = @()
    # `run-00-*` 是热身那一趟（Invoke-OneRun -Index 0，只留最后一次的覆盖结果），不进结论 ——
    # 和上面 visibleMs 那一组的口径一致。
    foreach ($probeFile in @(Get-ChildItem -LiteralPath $ReportDir -Filter 'run-*-probe.json' -ErrorAction SilentlyContinue
            | Where-Object { $_.BaseName -notlike 'run-00-*' })) {
        $probeJson = Get-Content -LiteralPath $probeFile.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        $probeJsons += $probeJson
        $lines.Add('')
        $lines.Add("## 探针 $($probeFile.BaseName)")
        $lines.Add('')
        $lines.Add("- 数据目录：$($probeJson.dataDir)；Qt $($probeJson.qt)；跑之前主题：$($probeJson.themeBefore)")
        $lines.Add('')
        $lines.Add('| 步骤 | 动作 ms | 整窗重画 ms |')
        $lines.Add('| --- | ---: | ---: |')
        foreach ($step in $probeJson.steps) {
            $lines.Add(("| {0} | {1} | {2} |" -f $step.name, $step.actionMs, $step.repaintMs))
        }
        $lines.Add('')
        $lines.Add(("主线程：看门狗 {0} ms，最长空档 {1} ms（减掉节拍 {2} ms），超过 50 ms 的节拍 {3} 次 / 共 {4} 次。" -f
                $probeJson.stalls.tickMs, $probeJson.stalls.maxGapMs, $probeJson.stalls.maxStallMs,
                $probeJson.stalls.over50ms, $probeJson.stalls.ticks))
    }
    # 跑不止一次时再给一张中位表：每一次的抖动都能从上面几张表里看出来，但结论只认这一张。
    if ($probeJsons.Count -gt 1) {
        $names = [Collections.Generic.List[string]]::new()
        $actionBy = @{}
        $repaintBy = @{}
        foreach ($probeJson in $probeJsons) {
            foreach ($step in $probeJson.steps) {
                $stepName = [string]$step.name
                if (-not $actionBy.ContainsKey($stepName)) {
                    $names.Add($stepName)
                    $actionBy[$stepName] = [Collections.Generic.List[double]]::new()
                    $repaintBy[$stepName] = [Collections.Generic.List[double]]::new()
                }
                $actionBy[$stepName].Add([double]$step.actionMs)
                $repaintBy[$stepName].Add([double]$step.repaintMs)
            }
        }
        $lines.Add('')
        $lines.Add("## 探针中位（n=$($probeJsons.Count)，偶数取偏低那一个）")
        $lines.Add('')
        $lines.Add('| 步骤 | 动作 ms | 整窗重画 ms |')
        $lines.Add('| --- | ---: | ---: |')
        foreach ($stepName in $names) {
            $actions = @($actionBy[$stepName] | Sort-Object)
            $repaints = @($repaintBy[$stepName] | Sort-Object)
            $actionMedian = $actions[[int][Math]::Floor(($actions.Count - 1) / 2)]
            $repaintMedian = $repaints[[int][Math]::Floor(($repaints.Count - 1) / 2)]
            $lines.Add(("| {0} | {1} | {2} |" -f $stepName, $actionMedian, $repaintMedian))
        }
        $worstGap = ($probeJsons | ForEach-Object { [int64]$_.stalls.maxGapMs } | Measure-Object -Maximum).Maximum
        $lines.Add('')
        $lines.Add("主线程最差一次：最长空档 $worstGap ms（含 20 ms 节拍）。")
    }
}
$lines.Add('')
$lines.Add("注册表：备份=$([bool]$hadKey)；还原逐字节一致=$restoredVerbatim")
if ($notes.Count) {
    $lines.Add('')
    $lines.Add('说明：')
    foreach ($n in $notes) { $lines.Add("- $n") }
}
$summaryPath = Join-Path $ReportDir 'summary.md'
$lines | Set-Content -LiteralPath $summaryPath -Encoding utf8

Write-Host ''
Write-Host "报告：$jsonPath"
Write-Host "摘要：$summaryPath"
if ($failure -or -not $restoredVerbatim -or $runResults.Count -eq 0) { exit 1 }
exit 0
