#Requires -Version 7.0
<#
.SYNOPSIS
    真机判定「窗口失焦后磨砂还在不在」：自己的实例，聚焦态 / 失焦态 / 背后原样 三张同区域截图比数字。

.DESCRIPTION
    流程（全程不调 SetForegroundWindow、不 SendInput，只动自己起的那个实例）：
      1. 起一个自己的实例（`-Exe`），挪到固定位置并置顶（不激活）；
      2. 实例上屏时系统会把前台给它（Qt 自己 ShowWindow），这时截「聚焦态」；没拿到就不截；
      3. `ShowWindow(SW_HIDE)` 让它失活，再 `SW_SHOWNOACTIVATE` 显示回来 —— 此时是「失活过的
         非激活窗口」，正是用户截图里那种状态，截「失焦态」；
      4. 再藏起来截「背后原样」做基线。

    取样区自动挑：在聚焦态整窗图里按 48x48 滑动找梯度最小的一块（正文/图标之外的空背景），
    三种状态都量这一块。三个数：
      - std  亮度标准差：平色 ≈ 0；
      - grad 相邻像素梯度：真模糊被压到 0.1~0.4，清晰桌面 0.7 以上；
      - 和基线几乎一样 => 整窗透明、根本没材质。

.PARAMETER Accent
    给子进程设 WTFML_ACCENT_ACRYLIC=1，走 accent 亚克力那条实验路径。

.PARAMETER Backdrop
    给子进程设 WTFML_BACKDROP（acrylic / mica / scene / solid）。

.EXAMPLE
    pwsh tools/defocus-glass.ps1 -Backdrop acrylic
    pwsh tools/defocus-glass.ps1 -Backdrop acrylic -Accent

.NOTES
    退出码：0 = 失焦仍是模糊；1 = 失焦退成平色 / 没有材质；2 = 没采到（窗口没起来、没拿到聚焦态）。
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/verification-mingw13/WTFMinecraftLauncher.exe',
    [ValidateSet('acrylic', 'mica', 'scene', 'solid')][string]$Backdrop = 'acrylic',
    [switch]$Accent,
    [string]$Out = 'out/defocus',
    [int]$WaitWindowSec = 30,
    [int]$WinX = 120,
    [int]$WinY = 120,
    [int]$WinW = 900,
    [int]$WinH = 620,
    [switch]$KeepWindow
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Dg {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
  public static void DpiAware() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
  public static IntPtr MainWindow(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      RECT r; GetWindowRect(h, out r);
      if (r.Right - r.Left < 200) return true;
      var t = new StringBuilder(256); GetWindowTextW(h, t, 256);
      if (t.Length == 0) return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }
}
'@
[Dg]::DpiAware() | Out-Null

$repo = Split-Path $PSScriptRoot -Parent
$exePath = [IO.Path]::GetFullPath($Exe, $repo)
if (-not (Test-Path -LiteralPath $exePath)) { throw "找不到 exe：$exePath（先跑 tools/dev-build.ps1）" }
$outDir = [IO.Path]::GetFullPath($Out, $repo)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$buildDir = Split-Path $exePath -Parent
$qtPrefix = 'D:/Qt/Qt/6.10.1/mingw_64'
$mingwBin = 'D:/Qt/Qt/Tools/mingw1310_64/bin'
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([A-Za-z0-9_]+)(?::[A-Z]+)?=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    if ($cache['CMAKE_PREFIX_PATH']) { $qtPrefix = ($cache['CMAKE_PREFIX_PATH'] -split ';')[0] }
    elseif ($cache['Qt6Core_DIR']) { $qtPrefix = [IO.Path]::GetFullPath((Join-Path $cache['Qt6Core_DIR'] '../../..')) }
    if ($cache['CMAKE_CXX_COMPILER']) { $mingwBin = Split-Path -Parent $cache['CMAKE_CXX_COMPILER'] }
}

function Get-Shot([int]$x, [int]$y, [int]$w, [int]$h, [string]$tag) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size($w, $h)), [System.Drawing.CopyPixelOperation]::SourceCopy)
    $g.Dispose()
    if ($tag -ne '') { $bmp.Save((Join-Path $outDir "$tag.png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    return $bmp
}

function Get-Lum([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $stride = $data.Stride
    $bmp.UnlockBits($data)
    $lum = New-Object double[] ($w * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + $x * 4
            $lum[$y * $w + $x] = 0.299 * $bytes[$i + 2] + 0.587 * $bytes[$i + 1] + 0.114 * $bytes[$i]
        }
    }
    return @{ Lum = $lum; W = $w; H = $h }
}

function Measure-Block($lum, [int]$w, [int]$h, [int]$x0, [int]$y0, [int]$bw, [int]$bh) {
    $sum = 0.0; $sum2 = 0.0; $n = 0; $grad = 0.0; $cnt = 0
    for ($y = $y0; $y -lt ($y0 + $bh); $y++) {
        for ($x = $x0; $x -lt ($x0 + $bw); $x++) {
            $l = $lum[$y * $w + $x]
            $sum += $l; $sum2 += $l * $l; $n++
            if ($x + 1 -lt $w -and $y + 1 -lt $h) {
                $grad += [math]::Abs($lum[$y * $w + $x + 1] - $l) + [math]::Abs($lum[($y + 1) * $w + $x] - $l)
                $cnt += 2
            }
        }
    }
    $mean = $sum / $n
    [pscustomobject]@{
        Mean = [math]::Round($mean, 2)
        Std  = [math]::Round([math]::Sqrt([math]::Max(0, $sum2 / $n - $mean * $mean)), 2)
        Grad = [math]::Round($grad / [math]::Max(1, $cnt), 3)
    }
}

$saved = @{ PATH = $env:PATH; QT_PLUGIN_PATH = $env:QT_PLUGIN_PATH; WTFML_BACKDROP = $env:WTFML_BACKDROP; WTFML_ACCENT_ACRYLIC = $env:WTFML_ACCENT_ACRYLIC }
$proc = $null
$exit = 2
try {
    $env:PATH = (@((Join-Path $qtPrefix 'bin'), $mingwBin, $env:PATH) -join ';')
    $env:QT_PLUGIN_PATH = Join-Path $qtPrefix 'plugins'
    $env:WTFML_BACKDROP = $Backdrop
    if ($Accent) { $env:WTFML_ACCENT_ACRYLIC = '1' } else { Remove-Item Env:WTFML_ACCENT_ACRYLIC -ErrorAction SilentlyContinue }

    $proc = Start-Process -FilePath $exePath -WorkingDirectory $repo -PassThru
    $hwnd = [IntPtr]::Zero
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $WaitWindowSec * 1000) {
        $hwnd = [Dg]::MainWindow([uint32]$proc.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
        if ($proc.HasExited) { throw "实例退出了（exit $($proc.ExitCode)）" }
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "等不到实例的主窗口（$WaitWindowSec s）" }

    # 只挪位置 + 置顶，不激活（SWP_NOACTIVATE）。实例上屏时系统会把前台给它，这里不碰。
    [Dg]::SetWindowPos($hwnd, [Dg]::HWND_TOPMOST, $WinX, $WinY, $WinW, $WinH, 0x0010) | Out-Null
    Start-Sleep -Milliseconds 1800

    $rect = New-Object Dg+RECT
    [Dg]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $cw = $rect.Right - $rect.Left
    $ch = $rect.Bottom - $rect.Top
    # 窗口有最小尺寸，可能比我要求的大：裁到屏幕内再取。
    $screenW = [Dg]::GetSystemMetrics(0)
    $screenH = [Dg]::GetSystemMetrics(1)
    if ($rect.Left + $cw -gt $screenW) { $rect.Left = [math]::Max(0, $screenW - $cw) }
    if ($rect.Top + $ch -gt $screenH) { $rect.Top = [math]::Max(0, $screenH - $ch) }
    $cw = [math]::Min($cw, $screenW - $rect.Left)
    $ch = [math]::Min($ch, $screenH - $rect.Top)

    $focused = $null
    if ([Dg]::GetForegroundWindow() -eq $hwnd) {
        $focused = Get-Shot $rect.Left $rect.Top $cw $ch 'focused-window'
    }

    # 失焦：最小化让它失活（前台自己回到别的窗口，我们不指定），再用 SW_SHOWNOACTIVATE 显示回来。
    [Dg]::ShowWindow($hwnd, 6) | Out-Null   # SW_MINIMIZE
    Start-Sleep -Milliseconds 800
    [Dg]::ShowWindow($hwnd, 4) | Out-Null   # SW_SHOWNOACTIVATE
    Start-Sleep -Milliseconds 1200
    $isForeground = [Dg]::GetForegroundWindow() -eq $hwnd
    $unfocusedShot = Get-Shot $rect.Left $rect.Top $cw $ch 'unfocused-window'
    Start-Sleep -Milliseconds 700
    $unfocusedShot2 = Get-Shot $rect.Left $rect.Top $cw $ch 'unfocused-window-2'

    [Dg]::ShowWindow($hwnd, 6) | Out-Null
    Start-Sleep -Milliseconds 600
    $baselineShot = Get-Shot $rect.Left $rect.Top $cw $ch 'baseline-window'

    # 自动挑取样块：在聚焦图（没拿到就用失焦图）里找梯度最小的 48x48。
    $pick = if ($focused) { $focused } else { $unfocusedShot }
    $pickShot = Get-Lum $pick
    $blockW = 48; $blockH = 48
    $best = $null; $bestGrad = [double]::MaxValue
    for ($y = 8; $y -le $ch - $blockH - 8; $y += 16) {
        for ($x = 8; $x -le $cw - $blockW - 8; $x += 16) {
            $m = Measure-Block $pickShot.Lum $cw $ch $x $y $blockW $blockH
            if ($m.Grad -lt $bestGrad) { $bestGrad = $m.Grad; $best = @{ X = $x; Y = $y } }
        }
    }
    $sx = $rect.Left + $best.X
    $sy = $rect.Top + $best.Y

    $focusedStats = if ($focused) { Measure-Block $pickShot.Lum $cw $ch $best.X $best.Y $blockW $blockH } else { $null }
    $uShot = Get-Lum $unfocusedShot
    $uShot2 = Get-Lum $unfocusedShot2
    # 整窗逐像素差：聚焦 vs 失焦、失焦 vs 失焦（噪声底）。主判据看这两个数。
    $changeMean = 0.0; $changePixels = 0
    $noiseMean = 0.0
    $n = $cw * $ch
    for ($i = 0; $i -lt $n; $i++) {
        $d = [math]::Abs($pickShot.Lum[$i] - $uShot.Lum[$i])
        if ($focused) { $changeMean += $d; if ($d -gt 8) { $changePixels++ } }
        $noiseMean += [math]::Abs($uShot.Lum[$i] - $uShot2.Lum[$i])
    }
    $changeMean = [math]::Round($changeMean / $n, 2)
    $noiseMean = [math]::Round($noiseMean / $n, 2)
    $changePct = [math]::Round(100.0 * $changePixels / $n, 1)
    $unfocusedStats = Measure-Block $uShot.Lum $cw $ch $best.X $best.Y $blockW $blockH
    $bShot = Get-Lum $baselineShot
    $baselineStats = Measure-Block $bShot.Lum $cw $ch $best.X $best.Y $blockW $blockH

    $row = [pscustomobject]@{
        Time       = (Get-Date).ToString('s')
        Backdrop   = $Backdrop
        Accent     = [bool]$Accent
        Sample     = "$sx,$sy ${blockW}x${blockH}"
        Focused    = if ($focusedStats) { "L=$($focusedStats.Mean) std=$($focusedStats.Std) grad=$($focusedStats.Grad)" } else { '未采到（实例没拿到前台）' }
        Unfocused  = "L=$($unfocusedStats.Mean) std=$($unfocusedStats.Std) grad=$($unfocusedStats.Grad)"
        Baseline   = "L=$($baselineStats.Mean) std=$($baselineStats.Std) grad=$($baselineStats.Grad)"
        WholeWindowDiff = "聚焦vs失焦 mean=$changeMean 变了$changePct% / 噪声底 mean=$noiseMean"
        ForegroundAfterShow = $isForeground
    }
    $row | Export-Csv -Path (Join-Path $outDir 'defocus.csv') -NoTypeInformation -Append -Encoding UTF8
    $row | Format-List | Out-String | Write-Host

    $same = ($focusedStats -and $changeMean -lt [math]::Max(1.5, 2.0 * $noiseMean))
    if (-not $focusedStats) {
        "判定：没拿到聚焦态，只测到失焦（std=$($unfocusedStats.Std) grad=$($unfocusedStats.Grad)），不能下结论。"
        $exit = 2
    } elseif ($same -and $unfocusedStats.Std -lt 1.5 -and $unfocusedStats.Grad -lt 0.05) {
        "判定：聚焦/失焦都是平色 —— 这一档本来就没有磨砂。"
        $exit = 1
    } elseif ($same) {
        "判定：切焦点前后整窗没变（差值 $changeMean ≈ 噪声底 $noiseMean）—— 磨砂在失焦后还在。"
        $exit = 0
    } elseif ($unfocusedStats.Std -lt 1.5 -and $unfocusedStats.Grad -lt 0.05) {
        "判定：失焦退成平色（聚焦 std=$($focusedStats.Std) → 失焦 std=$($unfocusedStats.Std)）。"
        $exit = 1
    } elseif ([math]::Abs($unfocusedStats.Mean - $baselineStats.Mean) -lt 0.6 -and [math]::Abs($unfocusedStats.Std - $baselineStats.Std) -lt 0.6) {
        "判定：失焦时没有材质（和背后原样一样）。"
        $exit = 1
    } else {
        "判定：切焦点时整窗有 $changePct% 的像素变了（差值 $changeMean，噪声底 $noiseMean）—— 失焦动了材质（聚焦 std=$($focusedStats.Std) / 失焦 std=$($unfocusedStats.Std)）。"
        $exit = 1
    }
    "截图与 CSV：$outDir"
} finally {
    foreach ($k in $saved.Keys) {
        if ($null -eq $saved[$k]) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue } else { Set-Item "Env:$k" $saved[$k] }
    }
    if (-not $KeepWindow -and $proc -and -not $proc.HasExited) {
        try { $proc.Kill(); $proc.WaitForExit(3000) | Out-Null } catch { }
    }
}
exit $exit



