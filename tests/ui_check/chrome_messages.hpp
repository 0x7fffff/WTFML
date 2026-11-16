/**
 * @file chrome_messages.hpp
 * @brief `chrome_messages.cpp` 的声明：把原生消息拼好交给 `WindowChrome::handleNativeEvent()`。
 *
 * 存在的理由（两句话）：`<windows.h>` 不能进 `ui_check/main.cpp`（那个文件里有个叫 `near`
 * 的局部变量，会被 `windef.h` 的上古宏吃掉），而 Z11 那两条契约断言又必须拼一个 `MSG`。
 * 细节见 `chrome_messages.cpp` 的文件头。
 */

#pragma once

#include <QtGlobal>

class WindowChrome;

/// WM_NCCALCSIZE 的"非客户区吃掉多少"那一问（wParam = TRUE）。没有真 HWND 也问得出来。
bool chromeHandlesNcCalcsize(WindowChrome *chrome, qintptr *result);

/// 随便一条别的消息（不带参数）—— 用来验"只认那两条，别的什么都不碰"。
bool chromeHandlesPlainMessage(WindowChrome *chrome, unsigned message, qintptr *result);

/// 拼一条改大小的 WM_WINDOWPOSCHANGING（cx、cy 非零、标志位 @p flags）交给外框，返回处理后的标志位。
/// 外框认定是贴靠时会补上 SWP_NOSIZE | SWP_NOMOVE。
unsigned chromeWindowPosChangingFlags(WindowChrome *chrome, unsigned flags);

/// SWP_NOSIZE | SWP_NOMOVE 的数值。
unsigned chromeSwpNoSizeNoMove();

/// WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE 的数值（系统缩放边框模态循环的进出）。
unsigned chromeEnterSizeMoveMessage();
unsigned chromeExitSizeMoveMessage();

/// WM_NCLBUTTONDOWN 的数值（常数从这里出，免得调用方为了一个数字去 include `<windows.h>`）。
unsigned chromeLeftButtonDownMessage();

/// WM_NCHITTEST 的数值（同上）。
unsigned chromeHitTestMessage();

