#Requires -Version 7.0
<#
.SYNOPSIS
    真机判定「窗口失焦后磨砂还在不在」：起自己的实例，聚焦 / 失焦各截整窗，比数字。

.DESCRIPTION
    只动本脚本自己的窗口：
      1. 先铺一块自己的红蓝棋盘底窗（置顶、不抢焦点），实例摆在它正上方 —— 背后有纹理，
         磨砂是活的就能看出模糊的红蓝，退成平色就是一块匀色；
      2. 起一个自己的实例（`-Exe`），系统把前台给它；用 UI 自动化点 `-Page` 那个导航入口
         （主页铺满不透明的清晰风景，量不出背板，所以默认去「设置」），截「聚焦态」；
      3. 本脚本自己的一个小窗接过前台（借实例的输入线程 `AttachThreadInput`，不碰别人的窗口），
         实例变成失焦窗口，截「失焦态」两张（第二张量噪声）；
      4. 失焦时把棋盘红蓝对调再截一张：材质是活的，窗口跟着变；平色则一个像素都不动；
      5. 杀掉实例，前台还给脚本开始时的那个窗口。

    三个数（整窗逐像素 RGB 三通道差的均值；亚克力 / 云母会把明度拉平，只比亮度看不出背后换色）：
      - change：聚焦 vs 失焦。≈ noise 说明失焦前后一样；
      - live：失焦时换背后颜色前后。和聚焦时一样大说明失焦时磨砂还在透背后；
        云母取的是壁纸、不透背后窗口，聚焦时这个数就 ≈ noise，这时只看 change；
      - noise：两张失焦图之间。

.PARAMETER Backdrop
    给子进程设 WTFML_BACKDROP（acrylic / mica / scene / solid）。

.PARAMETER Theme
    临时把 HKCU 的 Theme/Preset 写成这个主题（emerald / ocean / dark），结束时还原（原来没有就删掉）。

.PARAMETER Page
    量哪一页：导航入口的无障碍名（默认「设置」），用 UI 自动化选中，不发鼠标键盘。

.PARAMETER Rollback
    给子进程设 WTFML_KEEP_BACKDROP_ACTIVE=0（系统默认的失焦退色），做对照：这时应判失败。

.EXAMPLE
    pwsh tools/defocus-glass.ps1 -Backdrop acrylic -Theme dark
    pwsh tools/defocus-glass.ps1 -Backdrop acrylic -Rollback

.NOTES
    退出码：0 = 失焦前后一样（亚克力下还透背后）；1 = 失焦变了 / 退成平色；2 = 没采到（窗口没起来、没拿到前台、交不出前台）。
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/verification-mingw13/WTFMinecraftLauncher.exe',
    [ValidateSet('acrylic', 'mica', 'scene', 'solid')][string]$Backdrop = 'acrylic',
    [ValidateSet('', 'emerald', 'ocean', 'dark')][string]$Theme = '',
    [string]$Page = '设置',
    [switch]$Rollback,
    [string]$Out = 'out/defocus',
    [int]$WaitWindowSec = 30,
    [int]$WinX = 160,
    [int]$WinY = 120,
    [int]$WinW = 1000,
    [int]$WinH = 660,
    [switch]$KeepWindow
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes
# .NET 10 起 WinForms / GDI+ 拆出了 System.Private.Windows.*，按目录一并引用。
$desktop = Split-Path ([System.Windows.Forms.Form].Assembly.Location)
$refs = @('System.Windows.Forms', 'System.Drawing', 'System.Drawing.Common', 'System.Drawing.Primitives', 'System.Windows.Forms.Primitives',
          'System.ComponentModel.Primitives', 'System.Runtime.InteropServices', 'System.Collections') + (Get-ChildItem $desktop -Filter 'System.Private.Windows*.dll').FullName
Add-Type -ReferencedAssemblies $refs -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Text;
using System.Windows.Forms;
using System.Runtime.InteropServices;
public static class Dg {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
  /// 看得见的外框（DWMWA_EXTENDED_FRAME_BOUNDS）：不含左右下那圈看不见的缩放边，那圈直接露背后，不算窗口。
  public static RECT VisibleRect(IntPtr h) {
    RECT r;
    if (DwmGetWindowAttribute(h, 9, out r, Marshal.SizeOf(typeof(RECT))) != 0) GetWindowRect(h, out r);
    return r;
  }
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
  /// 把前台从 owner（本脚本自己起的实例）交给 to（本脚本自己的小窗）：挂上它的输入线程再 SetForegroundWindow。
  public static bool HandOver(IntPtr owner, IntPtr to) {
    uint pid; uint other = GetWindowThreadProcessId(owner, out pid);
    uint me = GetCurrentThreadId();
    bool attached = other != 0 && other != me && AttachThreadInput(me, other, true);
    bool ok = SetForegroundWindow(to);
    if (attached) AttachThreadInput(me, other, false);
    return ok && GetForegroundWindow() == to;
  }
  public static Bitmap Checker(int w, int h, int cell, bool swap) {
    var bmp = new Bitmap(w, h);
    using (var g = Graphics.FromImage(bmp)) {
      var a = new SolidBrush(Color.FromArgb(225, 40, 40)); var b = new SolidBrush(Color.FromArgb(40, 70, 230));
      for (int y = 0; y < h; y += cell) for (int x = 0; x < w; x += cell)
        g.FillRectangle((((x / cell) + (y / cell)) % 2 == 0) != swap ? a : b, x, y, cell, cell);
    }
    return bmp;
  }
  public static Bitmap Shot(int x, int y, int w, int h) {
    var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
    using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(x, y, 0, 0, new Size(w, h), CopyPixelOperation.SourceCopy);
    return bmp;
  }
  static byte[] Bytes(Bitmap bmp, out int stride) {
    var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    var bytes = new byte[data.Stride * bmp.Height];
    Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
    stride = data.Stride;
    bmp.UnlockBits(data);
    return bytes;
  }
  static double[] Lum(Bitmap bmp) {
    int stride; var bytes = Bytes(bmp, out stride);
    var lum = new double[bmp.Width * bmp.Height];
    for (int y = 0; y < bmp.Height; y++) for (int x = 0; x < bmp.Width; x++) {
      int i = y * stride + x * 4;
      lum[y * bmp.Width + x] = 0.299 * bytes[i + 2] + 0.587 * bytes[i + 1] + 0.114 * bytes[i];
    }
    return lum;
  }
  /// 两张同尺寸图逐像素颜色差（三通道差的均值）：[均值, 最大通道差 > 8 的像素占比 %]。
  public static double[] Diff(Bitmap a, Bitmap b) {
    int sa, sb; byte[] ba = Bytes(a, out sa), bb = Bytes(b, out sb);
    double sum = 0; int big = 0;
    for (int y = 0; y < a.Height; y++) for (int x = 0; x < a.Width; x++) {
      int i = y * sa + x * 4, j = y * sb + x * 4;
      int dr = Math.Abs(ba[i + 2] - bb[j + 2]), dg = Math.Abs(ba[i + 1] - bb[j + 1]), db = Math.Abs(ba[i] - bb[j]);
      sum += (dr + dg + db) / 3.0;
      if (Math.Max(dr, Math.Max(dg, db)) > 8) big++;
    }
    double n = (double)a.Width * a.Height;
    return new double[] { Math.Round(sum / n, 2), Math.Round(100.0 * big / n, 1) };
  }
  /// 整张图的亮度标准差（平色 ≈ 0）。
  public static double Std(Bitmap bmp) {
    double[] l = Lum(bmp); double s = 0, s2 = 0;
    foreach (double v in l) { s += v; s2 += v * v; }
    double m = s / l.Length;
    return Math.Round(Math.Sqrt(Math.Max(0, s2 / l.Length - m * m)), 2);
  }
}
/// 背后那块棋盘：置顶、不进任务栏、显示时不抢焦点。
public class Board : Form {
  public Board() { FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual; TopMost = true; AutoScaleMode = AutoScaleMode.None; }
  protected override bool ShowWithoutActivation { get { return true; } }
  protected override CreateParams CreateParams { get { var cp = base.CreateParams; cp.ExStyle |= 0x08000000 | 0x80; return cp; } }
}
/// 接前台的小窗：能被激活，显示时先不抢。
public class Taker : Form {
  public Taker() { FormBorderStyle = FormBorderStyle.FixedToolWindow; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual; TopMost = true; AutoScaleMode = AutoScaleMode.None; Text = "defocus-glass"; }
  protected override bool ShowWithoutActivation { get { return true; } }
}
'@
[Dg]::DpiAware() | Out-Null

function Wait-Pump([int]$ms) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 15 }
}

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

$themeKey = 'HKCU:\Software\WTFML\WTFML\Theme'
$savedPreset = $null; $hadPreset = $false; $createdKey = $false
$saved = @{ PATH = $env:PATH; QT_PLUGIN_PATH = $env:QT_PLUGIN_PATH; WTFML_BACKDROP = $env:WTFML_BACKDROP; WTFML_KEEP_BACKDROP_ACTIVE = $env:WTFML_KEEP_BACKDROP_ACTIVE }
$userForeground = [Dg]::GetForegroundWindow()
$proc = $null; $board = $null; $taker = $null
$shots = @{}
$exit = 2
try {
    if ($Theme) {
        $item = Get-ItemProperty -Path $themeKey -Name Preset -ErrorAction SilentlyContinue
        if ($item) { $hadPreset = $true; $savedPreset = $item.Preset }
        # 不能 New-Item -Force：对已有的注册表键它会整个重建，把别的值清掉。
        if (-not (Test-Path -LiteralPath $themeKey)) { New-Item -Path $themeKey | Out-Null; $createdKey = $true }
        Set-ItemProperty -Path $themeKey -Name Preset -Value $Theme
    }

    # 棋盘比实例四周各宽 48px（实例 GetWindowRect 里那圈看不见的缩放边也落在棋盘上）；接前台的小窗放棋盘左上角外侧。
    $pad = 48
    $board = [Board]::new()
    $board.Bounds = [System.Drawing.Rectangle]::new($WinX - $pad, $WinY - $pad, $WinW + 2 * $pad, $WinH + 2 * $pad)
    $checkerA = [Dg]::Checker($board.Width, $board.Height, 80, $false)
    $checkerB = [Dg]::Checker($board.Width, $board.Height, 80, $true)
    $board.BackgroundImage = $checkerA
    $board.Show()
    $taker = [Taker]::new()
    $taker.Bounds = [System.Drawing.Rectangle]::new([math]::Max(0, $WinX - $pad - 200), [math]::Max(0, $WinY - $pad - 80), 180, 60)
    $taker.Show()
    Wait-Pump 300

    $env:PATH = (@((Join-Path $qtPrefix 'bin'), $mingwBin, $env:PATH) -join ';')
    $env:QT_PLUGIN_PATH = Join-Path $qtPrefix 'plugins'
    $env:WTFML_BACKDROP = $Backdrop
    if ($Rollback) { $env:WTFML_KEEP_BACKDROP_ACTIVE = '0' } else { Remove-Item Env:WTFML_KEEP_BACKDROP_ACTIVE -ErrorAction SilentlyContinue }

    $proc = Start-Process -FilePath $exePath -WorkingDirectory $repo -PassThru
    $hwnd = [IntPtr]::Zero
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $WaitWindowSec * 1000) {
        $hwnd = [Dg]::MainWindow([uint32]$proc.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Wait-Pump 200
        if ($proc.HasExited) { throw "实例退出了（exit $($proc.ExitCode)）" }
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "等不到实例的主窗口（$WaitWindowSec s）" }

    # 摆到棋盘正上方并置顶，不激活（SWP_NOACTIVATE）。
    [Dg]::SetWindowPos($hwnd, [Dg]::HWND_TOPMOST, $WinX, $WinY, $WinW, $WinH, 0x0010) | Out-Null
    Wait-Pump 1500
    if ($Page) {
        # 导航轨上的页面入口是按钮（无障碍名 = 页名），走 Invoke。
        $ua = [System.Windows.Automation.AutomationElement]
        $cond = [System.Windows.Automation.PropertyCondition]::new($ua::NameProperty, $Page)
        $entry = $ua::FromHandle($hwnd).FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        $pattern = $null
        if (-not $entry -or -not $entry.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
            "判定：实例里找不到能点的「$Page」入口，没法换页。"
            exit 2
        }
        $pattern.Invoke()
    }
    Wait-Pump 2000

    # 窗口有最小尺寸 / 按 DPI 调过，实际大小可能和要的不一样：棋盘按实际外框重铺。
    $rect = New-Object Dg+RECT
    [Dg]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $board.Bounds = [System.Drawing.Rectangle]::new($rect.Left - $pad, $rect.Top - $pad, $rect.Right - $rect.Left + 2 * $pad, $rect.Bottom - $rect.Top + 2 * $pad)
    $checkerA = [Dg]::Checker($board.Width, $board.Height, 80, $false)
    $checkerB = [Dg]::Checker($board.Width, $board.Height, 80, $true)
    $board.BackgroundImage = $checkerA
    $board.Refresh()
    Wait-Pump 800
    $rect = [Dg]::VisibleRect($hwnd)
    $sx = [math]::Max(0, $rect.Left); $sy = [math]::Max(0, $rect.Top)
    $cw = [math]::Min($rect.Right, [Dg]::GetSystemMetrics(0)) - $sx
    $ch = [math]::Min($rect.Bottom, [Dg]::GetSystemMetrics(1)) - $sy
    function Snap([string]$tag) {
        $bmp = [Dg]::Shot($sx, $sy, $cw, $ch)
        $bmp.Save((Join-Path $outDir "$tag.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $shots[$tag] = $bmp
    }
    function Set-Checker($img) { $board.BackgroundImage = $img; $board.Refresh(); Wait-Pump 900 }

    $gotFocus = [Dg]::GetForegroundWindow() -eq $hwnd
    if (-not $gotFocus) {
        "判定：实例上屏时没拿到前台（前台是别的窗口），采不到聚焦态。"
        exit 2
    }
    Snap 'focused'
    Set-Checker $checkerB
    Snap 'focused-swapped'
    Set-Checker $checkerA

    if (-not [Dg]::HandOver($hwnd, $taker.Handle)) {
        "判定：前台交不到脚本自己的小窗，采不到失焦态。"
        exit 2
    }
    Wait-Pump 1500
    Snap 'unfocused'
    Wait-Pump 700
    Snap 'unfocused-2'
    Set-Checker $checkerB
    Snap 'unfocused-swapped'
    Set-Checker $checkerA
    $stillUnfocused = [Dg]::GetForegroundWindow() -ne $hwnd

    [Dg]::ShowWindow($hwnd, 0) | Out-Null   # SW_HIDE：背后原样，只留着看图
    Wait-Pump 600
    Snap 'baseline'

    $change = [Dg]::Diff($shots['focused'], $shots['unfocused'])
    $noise = [Dg]::Diff($shots['unfocused'], $shots['unfocused-2'])
    $live = [Dg]::Diff($shots['unfocused'], $shots['unfocused-swapped'])
    $focusedLive = [Dg]::Diff($shots['focused'], $shots['focused-swapped'])
    $row = [pscustomobject]@{
        Time        = (Get-Date).ToString('s')
        Backdrop    = $Backdrop
        Theme       = if ($Theme) { $Theme } else { '(设置里的)' }
        Rollback    = [bool]$Rollback
        Region      = "$sx,$sy ${cw}x$ch"
        Change      = "聚焦vs失焦 mean=$($change[0]) 变了$($change[1])%"
        Live        = "失焦换背后 mean=$($live[0]) / 聚焦换背后 mean=$($focusedLive[0])"
        Noise       = $noise[0]
        Std         = "聚焦 $([Dg]::Std($shots['focused'])) / 失焦 $([Dg]::Std($shots['unfocused']))"
        StayedUnfocused = $stillUnfocused
    }
    $row | Export-Csv -Path (Join-Path $outDir 'defocus.csv') -NoTypeInformation -Append -Encoding UTF8
    $row | Format-List | Out-String | Write-Host

    # 静态页两张失焦图逐像素一样（噪声 0）；深色云母激活 / 失焦本来只差 0.6 左右，下限不能放宽。
    $same = $change[0] -le [math]::Max(0.3, 2.0 * $noise[0])
    # 聚焦时换背后颜色窗口明显跟着变 = 这一档透背后窗口（亚克力）；云母 / 风景 / 纯色不透，只看 change。
    $seesThrough = $focusedLive[0] -gt [math]::Max(3.0, 4.0 * $noise[0])
    $through = $live[0] -ge 0.5 * $focusedLive[0]
    if (-not $stillUnfocused) {
        "判定：截失焦图期间实例又拿回了前台，这组不算。"
        $exit = 2
    } elseif ($same -and (-not $seesThrough -or $through)) {
        "判定：失焦前后整窗一样（差 $($change[0]) ≈ 噪声 $($noise[0])）" + $(if ($seesThrough) { "，失焦时换背后颜色窗口跟着变（$($live[0])，聚焦时 $($focusedLive[0])）—— 磨砂还在。" } else { "（这一档不透背后窗口）。" })
        $exit = 0
    } elseif ($seesThrough -and $live[0] -le [math]::Max(1.0, 2.0 * $noise[0])) {
        "判定：失焦退成平色（换背后颜色窗口不动：$($live[0])；聚焦时 $($focusedLive[0])）。"
        $exit = 1
    } else {
        "判定：切焦点时整窗变了（差 $($change[0])，$($change[1])% 的像素，噪声 $($noise[0])）。"
        $exit = 1
    }
    "截图与 CSV：$outDir"
} finally {
    if (-not $KeepWindow -and $proc -and -not $proc.HasExited) {
        try { $proc.Kill(); $proc.WaitForExit(3000) | Out-Null } catch { }
    }
    # 前台还给开始时那个窗口（此刻前台若是脚本自己的小窗，本进程有权交出去）。
    if ($userForeground -ne [IntPtr]::Zero -and [Dg]::IsWindow($userForeground) -and [Dg]::GetForegroundWindow() -ne $userForeground) {
        [Dg]::SetForegroundWindow($userForeground) | Out-Null
    }
    if ($taker) { $taker.Close(); $taker.Dispose() }
    if ($board) { $board.Close(); $board.Dispose() }
    foreach ($bmp in $shots.Values) { $bmp.Dispose() }
    foreach ($k in $saved.Keys) {
        if ($null -eq $saved[$k]) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue } else { Set-Item "Env:$k" $saved[$k] }
    }
    if ($Theme) {
        if ($hadPreset) { Set-ItemProperty -Path $themeKey -Name Preset -Value $savedPreset }
        elseif ($createdKey) { Remove-Item -LiteralPath $themeKey -ErrorAction SilentlyContinue }
        else { Remove-ItemProperty -Path $themeKey -Name Preset -ErrorAction SilentlyContinue }
    }
}
exit $exit
