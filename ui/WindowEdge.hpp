/**
 * @file WindowEdge.hpp
 * @brief 窗口最外沿：不画线，让窗口边直接融进背板。
 *
 * Win11 上由 WindowChrome 把系统那条 1px 硬边去掉（DWMWA_BORDER_COLOR = NONE）；客户区边上**不再自画**
 * 线和淡出 —— DWM 在客户区外本来就留着 1px 可见边（亚克力 / 云母下是没着色的背板），里面再画一圈，
 * 两道边各自圆角、错开 1px，浅色主题左边、左下角看着像两层页面。窗口边只剩 DWM 的圆角裁切和系统阴影。
 *
 * Win10 不动（直角窗、系统边去不掉）。环境变量 `WTFML_WINDOW_EDGE=0` 关掉，回到系统默认边（对照用）。
 */

#pragma once

namespace WindowEdge {

/// 系统认不认（Win11 才有圆角与 DWMWA_BORDER_COLOR）。
bool supported();
/// supported() 且没被 WTFML_WINDOW_EDGE=0 关掉。
bool enabled();

}  // namespace WindowEdge
