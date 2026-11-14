#Requires -Version 7.0
<#
.SYNOPSIS
    真·产品 exe 的启动/就绪耗时与界面卡顿测量（外部测量，不改产品代码）。

.DESCRIPTION
    P2「测量＋优化｜构建与运行」的第二半。测的是 **Release（-O3）** 编出来的
    WTFMinecraftLauncher.exe —— 验证工程（mw_check/verify）按设计是 -O0，量出来的
    速度不能代表产品（docs/dev/TESTING.md 的性能规则）。

    设置不进用户注册表：每次运行都建一份临时验收配置，用
    `--acceptance-profile <临时目录>` 起 exe —— 设置落在 `<目录>/settings`、游戏目录
    落在 `<目录>/game`、智能体数据落在 `<目录>/agent`，夹具也造在 `<目录>/game` 里，
    跑完整个临时目录删掉。不碰用户注册表，也就不需要备份 / 还原。

    ⚠ 跑之前必须没有启动器在运行：机器上另有一份在跑会干扰测量。
    ⚠ 探针模式带首帧守卫：表面超过 50 ms 又不在允许名单里，这一趟以非零退出。

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
    # 给第一份预置实例（perf-instance-001）造一份"重实例"夹具：模组 / 资源包 / 光影 /
    # 数据包 / 存档 / 截图 / 日志 / 配置文件。加载态盘点（步骤 125）要量实例设置各页签
    # 首次铺开的成本，0 个模组的空实例量不出东西。默认 0 = 不造（老报告可比）。
    [int]$InstanceMods = 0,
    [int]$StallSeconds = 3,
    [int]$Warmup = 1,
    # 界面探针模式：给 exe 传 `--wtfml-perf-probe=<沙盒>/perf-probe.json`，由它自己
    # 按真实入口发动作（翻页 / 换主题）、自己退出。要求 exe 是
    # `-DWTFML_PERF_PROBE=ON` 的 Release 构建（见 docs/dev/PERFORMANCE.md）。
    # 这一档不做外部 WM_NULL 探测（会干扰探针自己量的节拍）。
    [switch]$Probe,
    # 探针模式的首帧允许名单：`步骤名<TAB>放行时的最大首帧`。默认
    # tools/perf-firstframe-allowlist.txt（只许删条目，不许加）。
    [string]$FirstFrameAllowlist,
    # 验收配置的临时根目录（`--acceptance-profile` 指向它）。默认 <报告目录>/profile，
    # 跑完整个删掉；设置 / 游戏目录 / 智能体数据都在这一份里。
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

if (-not $Sandbox) { $Sandbox = Join-Path $ReportDir 'profile' }
if (-not [IO.Path]::IsPathRooted($Sandbox)) { $Sandbox = Join-Path $repo $Sandbox }
$Sandbox = [IO.Path]::GetFullPath($Sandbox)
if (Test-Path -LiteralPath $Sandbox) { Remove-Item -LiteralPath $Sandbox -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Sandbox | Out-Null
$GameDir = Join-Path $Sandbox 'game'
$ProfileMarker = Join-Path $Sandbox 'acceptance-profile.json'

if (-not $FirstFrameAllowlist) { $FirstFrameAllowlist = Join-Path $repo 'tools/perf-firstframe-allowlist.txt' }
$FirstFrameAllowlist = [IO.Path]::GetFullPath($FirstFrameAllowlist)

# ---- 运行前的闸：用户自己的启动器在跑就拒绝；绝不替用户杀掉它。 ----
$running = @(Get-Process -Name 'WTFMinecraftLauncher' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "检测到启动器正在运行（PID $($running.Id -join ', ')）。先关掉再测：另有一份在跑会干扰测量。"
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

$versionsDir = Join-Path $GameDir 'versions'
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

# ---- 重实例夹具（-InstanceMods）：第一份实例里塞模组 / 资源包 / 存档 / 截图 / 日志 ----
# 加载态盘点要量实例设置那几个页签"首次铺开"的成本。空实例每个目录都是 0 项，量到的
# 只有控件构造；用户真机上那种 50+ 模组的实例才会走读盘 / 解析 / 缩略图那条路。
if ($InstanceMods -gt 0) {
    if ($Instances -lt 1) { throw "-InstanceMods 需要至少 1 份预置实例（-Instances >= 1）。" }
    $fixture = Join-Path $versionsDir 'perf-instance-001'
    # 一张 1x1 PNG：截图页要的是"能解码成图"的文件，不是任意字节。
    [byte[]]$png = [Convert]::FromBase64String(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==')
    $modsDir = Join-Path $fixture 'mods'
    New-Item -ItemType Directory -Force -Path $modsDir | Out-Null
    for ($i = 1; $i -le $InstanceMods; $i++) {
        Write-EmptyZip (Join-Path $modsDir ('fixture-mod-{0:D3}.jar' -f $i))
    }
    foreach ($pair in @(@('resourcepacks', 12, 'resourcepack'), @('shaderpacks', 6, 'shaderpack'),
                        @('datapacks', 5, 'datapack'))) {
        $dir = Join-Path $fixture $pair[0]
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        for ($i = 1; $i -le $pair[1]; $i++) {
            Write-EmptyZip (Join-Path $dir ('fixture-{0}-{1:D2}.zip' -f $pair[2], $i))
        }
    }
    $savesDir = Join-Path $fixture 'saves'
    for ($i = 1; $i -le 8; $i++) {
        $dir = Join-Path $savesDir ('FixtureWorld{0:D2}' -f $i)
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $dir 'level.dat'), [byte[]]::new(4096))
        Set-Content -LiteralPath (Join-Path $dir 'levelname.txt') -Value ('FixtureWorld{0:D2}' -f $i) -Encoding utf8
    }
    $shotsDir = Join-Path $fixture 'screenshots'
    New-Item -ItemType Directory -Force -Path $shotsDir | Out-Null
    for ($i = 1; $i -le 24; $i++) {
        [IO.File]::WriteAllBytes((Join-Path $shotsDir ('2026-11-14_12.{0:D2}.{1:D2}.png' -f ($i % 60), $i)), $png)
    }
    $logsDir = Join-Path $fixture 'logs'
    New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
    $logLines = for ($i = 1; $i -le 4000; $i++) {
        '[{0:D2}:{1:D2}:{2:D2}] [Render thread/INFO]: fixture log line {3} (sodium 0.6.13)' -f ($i % 24), ($i % 60), ($i % 60), $i
    }
    Set-Content -LiteralPath (Join-Path $logsDir 'latest.log') -Value $logLines -Encoding utf8
    $configDir = Join-Path $fixture 'config'
    New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    for ($i = 1; $i -le 24; $i++) {
        Set-Content -LiteralPath (Join-Path $configDir ('fixture-{0:D2}.json' -f $i)) `
            -Value ('{{ "quality": "high", "maxFps": {0} }}' -f (30 + $i)) -Encoding utf8
    }
    Set-Content -LiteralPath (Join-Path $fixture 'options.txt') -Value 'renderDistance:12' -Encoding utf8
}

# ---- 验收配置：设置 / 游戏目录 / 智能体数据都落在临时目录里，不碰用户注册表 ----
# `--acceptance-profile` 要求根目录里有一份标记文件（见 src/util/StartupAcceptance.hpp），
# closeAfterMs 是自动关窗的兜底；上限就是 120000（2 s~120 s 的配额）。
@{ format = 'wtfml-startup-acceptance/1'; closeAfterMs = 120000; stressRows = 0 } |
    ConvertTo-Json -Compress | Set-Content -LiteralPath $ProfileMarker -Encoding utf8 -NoNewline

$exeInfo = Get-Item -LiteralPath $Exe
$exeMd5 = (Get-FileHash -LiteralPath $Exe -Algorithm MD5).Hash.ToLowerInvariant()

# NOTE: 变量名**不能**叫 $runs —— PowerShell 变量不区分大小写，会把上面的
# [int]$Runs 参数整个盖掉，后面 `$i -le $Runs` 就变成"Int32 转 List"的绑定错误。
$runResults = [Collections.Generic.List[object]]::new()
$notes = [Collections.Generic.List[string]]::new()
$current = $null

function Invoke-OneRun {
    param([int]$Index, [bool]$Measure)
    $probeOut = if ($Probe) { Join-Path $Sandbox 'perf-probe.json' } else { $null }
    if ($probeOut -and (Test-Path -LiteralPath $probeOut)) { Remove-Item -LiteralPath $probeOut -Force }
    $profileArg = "--acceptance-profile=`"$Sandbox`""
    $proc = if ($Probe) {
        # 路径里可能有空格，参数整体带上引号再交给 Start-Process。
        $probeArg = "--wtfml-perf-probe=`"$probeOut`""
        $allowArg = "--wtfml-firstframe-allowlist=`"$FirstFrameAllowlist`""
        Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) `
            -ArgumentList @($profileArg, $probeArg, $allowArg) -PassThru
    } else {
        Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) `
            -ArgumentList @($profileArg) -PassThru
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

        $appLog = Join-Path $GameDir 'logs/wtfml.log'
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
        if ($exitCode -ne 0) {
            $guardNames = @()
            if ($probeCopy -and (Test-Path -LiteralPath $probeCopy)) {
                try {
                    $probeJson = Get-Content -LiteralPath $probeCopy -Raw -Encoding utf8 | ConvertFrom-Json
                    $guardNames = @($probeJson.firstFrameGuard.overLimit | ForEach-Object { $_.name })
                } catch { }
            }
            if ($guardNames.Count -gt 0) {
                throw ("第 {0} 次首帧守卫失败（超过 50 ms 且不在允许名单）：{1}。逐条数字见 {2}" -f
                    $Index, ($guardNames -join '、'), (Join-Path $ReportDir 'summary.md'))
            }
            throw ("第 {0} 次探针异常退出：exitCode={1}。日志保留在报告目录。" -f $Index, $exitCode)
        }
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

    $appLog = Join-Path $GameDir 'logs/wtfml.log'
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
}

if (-not $KeepSandbox) {
    # 验收配置里只有夹具、设置和这次跑出来的日志副本；日志已经另存到报告目录，
    # 留着临时目录只会占盘。整个删掉，用户注册表本来就没动过。
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
    instanceMods      = $InstanceMods
    sandbox           = [IO.Path]::GetRelativePath($repo, $Sandbox)
    measurement       = if ($Probe) {
        '产品内探针（WTFML_PERF_PROBE=1，Release）：Java/导航/主题、200条合成任务、1000条预设视图、临时预设元数据同步/后台读取；每步量动作+整窗repaint；20ms看门狗标记卡顿跨度。造数及故意同步读取不代表产品界面路径。'
    } else {
        '外部测量：进程创建 → 标题为 "WTF Minecraft Launcher" 的顶层窗口可见 → 应答 WM_NULL；窗口出现后观察主线程最长阻塞。'
    }
    acceptanceProfile = [ordered]@{
        root       = [IO.Path]::GetRelativePath($repo, $Sandbox)
        gameDir    = [IO.Path]::GetRelativePath($repo, $GameDir)
        keptOnDisk = [bool]$KeepSandbox
    }
    firstFrameAllowlist = if ($Probe) { [IO.Path]::GetRelativePath($repo, $FirstFrameAllowlist) } else { $null }
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
    # 首帧守卫只认 Release 探针的数（offscreen -O0 只能横向比），每趟探针自己判一次；
    # 这里把结论抄进摘要，失败时不用再翻 JSON。
    $guards = @($probeJsons | ForEach-Object { $_.firstFrameGuard } | Where-Object { $null -ne $_ })
    if ($guards.Count -gt 0) {
        $guard = $guards[-1]
        $allowlistCount = @(if (Test-Path -LiteralPath $FirstFrameAllowlist) {
                Get-Content -LiteralPath $FirstFrameAllowlist -Encoding utf8 |
                    Where-Object { $_.Trim() -and -not $_.TrimStart().StartsWith('#') }
            }).Count
        $lines.Add('')
        $lines.Add("## 首帧守卫（阈值 $($guard.thresholdMs) ms；等数据 waitMs 不算首帧）")
        $lines.Add('')
        $lines.Add("- 允许名单：$($guard.allowlistPath)（$allowlistCount 条，只许删不许加）")
        if ($guard.PSObject.Properties['error'] -and $guard.error) {
            $lines.Add("- 名单读取失败：$($guard.error)")
        }
        foreach ($entry in @($guard.overLimit)) {
            $lines.Add(("- 超阈值且不在名单：{0} 首帧 {1} ms（动作 {2} + 重画 {3}）" -f
                    $entry.name, $entry.firstFrameMs, $entry.actionMs, $entry.repaintMs))
        }
        foreach ($entry in @($guard.whitelisted)) {
            $lines.Add(("- 超阈值但在名单：{0} 首帧 {1} ms（放行时记录 {2} ms）" -f
                    $entry.name, $entry.firstFrameMs, $entry.allowlistValue))
        }
        foreach ($entry in @($guard.staleWhitelist)) {
            $lines.Add(("- 名单里但这次没超（先留着，稳定低于 50 ms 再删）：{0} 首帧 {1} ms" -f
                    $entry.name, $entry.firstFrameMs))
        }
        $lines.Add("- 结论：$(if ($guard.failed) { '失败' } else { '通过' })；这一趟看了 $($guard.surfaces) 个表面。")
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
$lines.Add("验收配置：$([IO.Path]::GetRelativePath($repo, $Sandbox))（--acceptance-profile；$(if ($KeepSandbox) { '保留' } else { '跑完已删' })）")
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
if ($failure -or $runResults.Count -eq 0) { exit 1 }
exit 0
