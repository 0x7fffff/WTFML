/**
 * @file WindowEdge.hpp
 * @brief 窗口最外沿：不画线，让窗口边直接融进背板。
 *
 * Win11 上由 WindowChrome 做两件事：把系统那条 1px 硬边去掉（DWMWA_BORDER_COLOR = NONE），
 * 并让客户区盖住 DWM 那 1px 可见边（否则那 1px 是一圈没着色的背板 / 透明缝，和客户区错开一像素，
 * 浅色主题左边、左下角看着像两层页面）。窗口边只剩 DWM 的圆角裁切和系统阴影。
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
