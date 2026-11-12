/**
 * @file WindowEdge.hpp
 * @brief 窗口最外沿：一圈 1px 上亮下淡的细线 + 往里几像素淡出，让窗口边融进磨砂底。
 *
 * 由 WindowChrome 装上（主窗口、游戏日志窗、ChromeDialog 都走它），盖在所有子控件上面、不吃鼠标；
 * 最大化 / 全屏时不画（贴着屏幕边没有"边"可言）。只在 Win11（build ≥ 22000）上画：Win10 窗口是直角、
 * 系统边去不掉，按圆角画会在四角留缺口。环境变量 `WTFML_WINDOW_EDGE=0` 整个关掉（对比样张用）。
 */

#pragma once

class QPainter;
class QRectF;
class QWidget;

namespace WindowEdge {

// ---- 要调就调这几个 ----
/// 上沿细线的不透明度（0..1）；往下线性淡到 kBottomAlpha。
inline constexpr double kTopAlpha = 0.28;
/// 下沿细线的不透明度（深色底，白线）。
inline constexpr double kBottomAlpha = 0.06;
/// 浅色底下沿那条深色收边的不透明度：浅色窗口放在浅色桌面上靠它分出边界。
inline constexpr double kLightBottomAlpha = 0.16;
/// 细线往里那一圈淡出的宽度（逻辑像素）；0 = 只画那根线。
inline constexpr int kFadeWidth = 3;

/// Win11 DWM 圆角（DWMWCP_ROUND）的半径，逻辑像素。
inline constexpr double kCornerRadius = 8.0;

/// 系统认不认（Win11 才有圆角与 DWMWA_BORDER_COLOR）。
bool supported();
/// supported() 且没被 WTFML_WINDOW_EDGE=0 关掉。
bool enabled();
/// 在 rect 的最外沿画那一圈。dark = 深色底（白线上亮下淡）；浅色底上沿白高光、下沿深色收边。
void paint(QPainter &painter, const QRectF &rect, bool dark);
/// 给顶层窗口盖一层只画边的透明子控件（重复调用无害）。
void install(QWidget *window);

}  // namespace WindowEdge
