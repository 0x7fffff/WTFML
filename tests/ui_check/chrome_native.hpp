/**
 * @file chrome_native.hpp
 * @brief `chrome_native.cpp` 的声明：对真 HWND 发 WM_NCHITTEST、量标题栏高度、数原生子窗口。
 *
 * 只在 Qt 平台插件是 `windows` 时有意义（offscreen 给的 winId 是假句柄）；其余平台返回 -1。
 * 单开一个 TU 的理由同 chrome_messages.hpp：`<windows.h>` 不进用例文件。
 */

#pragma once

#include <QPoint>

class QWidget;

/// 平台插件是不是真 windows。
bool chromeNativeAvailable();

/// 向 `window` 的 HWND 发 WM_NCHITTEST，点 = 窗口内的逻辑坐标 `local`（和 mapToGlobal 同一套）。
/// 返回 HT* 值，没有真 HWND 返回 -1。
int chromeNativeHitTest(QWidget *window, const QPoint &local);

/// 客户区顶比窗口顶低多少物理像素（系统标题栏收掉了 = 0）；没有真 HWND 返回 -1。
int chromeNativeCaptionHeight(QWidget *window);

/// `window` 的 HWND 下面有几个原生子窗口（对子控件调 winId() 会造出来）；没有真 HWND 返回 -1。
int chromeNativeChildCount(QWidget *window);

/// 鼠标落在屏幕物理点 `local`（窗口内逻辑坐标）时，真正收到鼠标的 HWND 是不是这个窗口自己
/// （被原生子窗口盖住就是 false）；没有真 HWND 返回 -1。
int chromeNativeMouseReachesWindow(QWidget *window, const QPoint &local);

/// HWND 上的 DWM 背板类型（`DWMWA_SYSTEMBACKDROP_TYPE`：1 无、2 云母、3 亚克力）；读不到或没有真 HWND 返回 -1。
int chromeNativeBackdropType(QWidget *window);

/// DWM 眼里这扇窗口的非客户区是不是激活态（`GetWindowInfo().dwWindowStatus == WS_ACTIVECAPTION`）：
/// 亚克力 / 云母只在激活态画材质，失焦时换平色。1 / 0；没有真 HWND 返回 -1。
int chromeNativeFrameActive(QWidget *window);
/// 给真 HWND 发一条 `WM_NCACTIVATE`（走 Qt 的窗口过程 → nativeEvent），模拟窗口得到 / 失去焦点时系统发的那条。
void chromeNativeSendNcActivate(QWidget *window, bool active);

/// HTCAPTION / HTCLIENT 的数值。
int chromeHtCaption();
/// HTTOP（HTTOPLEFT、HTTOPRIGHT 紧跟在后面）。
int chromeHtTop();
int chromeHtClient();
