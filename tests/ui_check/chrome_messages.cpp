/**
 * @file chrome_messages.cpp
 * @brief 一小块**单独编的**翻译单元：只为了让 `ui_check/main.cpp` 不必 include `<windows.h>`。
 *
 * 【为什么单开一个文件】
 * `ui_check/main.cpp` 有一万多行。往里面塞一个 `<windows.h>` 会同时踩两件事：
 *   1. `windef.h` 里那两个上古兼容宏（`near` / `far`）会把那片代码里同名的一个局部变量
 *      整个吃掉 —— 实测编不过，报在 `const bool near = …` 那一行（`near` 被展开成空）；
 *   2. `<windows.h>` 得排在 Qt 头**之前**才稳，而那个大文件里 Qt 头在最上面。
 * 为了两条断言让整个 TU 承担这些不划算。这里只暴露四个普通函数：把消息拼好交给
 * `WindowChrome::handleNativeEvent()`，再把结果带回去。
 *
 * NOTE: 这几个函数量的只是**契约**（认不认这条消息、回什么），不是真的 Win32 行为 ——
 *       真的那一份（客户区铺满整窗、缩放边、HTCAPTION、双击不最大化）是探针
 *       `build/tools/chromeprobe2` 挂到真 HWND 上量的。
 */

#include "chrome_messages.hpp"

#include <windows.h>   // 必须在 Qt 头之前（这个 TU 里没有别的 Qt 头，正是故意的）

#include "WindowChrome.hpp"

/// WM_NCCALCSIZE 的"非客户区吃掉多少"那一问（wParam = TRUE）。没有真 HWND 也问得出来：
/// 那个分支只用到 `IsZoomed(msg->hwnd)`，而 `IsZoomed(nullptr)` 回 FALSE，安全。
bool chromeHandlesNcCalcsize(WindowChrome *chrome, qintptr *result) {
    MSG msg{};
    msg.message = WM_NCCALCSIZE;
    msg.wParam = TRUE;   // 这一问才是"非客户区吃掉多少"；FALSE 那一问不碰
    msg.hwnd = nullptr;  // 见上面那段
    return chrome != nullptr && chrome->handleNativeEvent(&msg, result);
}

/// 随便一条别的消息（不带参数）—— 用来验"只认那两条，别的什么都不碰"。
bool chromeHandlesPlainMessage(WindowChrome *chrome, unsigned message, qintptr *result) {
    MSG msg{};
    msg.message = message;
    msg.hwnd = nullptr;
    return chrome != nullptr && chrome->handleNativeEvent(&msg, result);
}

/// 一条和"拖窗"有关、但**不是**我们该认的消息（左键按在非客户区上）。
/// 常数从这里出，免得调用方为了一个数字去 include `<windows.h>`。
unsigned chromeLeftButtonDownMessage() {
    return WM_NCLBUTTONDOWN;
}

/// 同上：WM_NCHITTEST。**故意只给常数、不给"拼一发假的"那个函数** ——
/// 命中测试要真 HWND 才谈得上"点在哪儿"（`GetWindowRect` 对空句柄直接失败），
/// 拿假 lParam 问出来的只会是"没认领"，那是验了等于没验。
unsigned chromeHitTestMessage() {
    return WM_NCHITTEST;
}
