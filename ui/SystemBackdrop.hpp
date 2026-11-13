/**
 * @file SystemBackdrop.hpp
 * @brief DWM 系统亚克力 / 云母长什么样：照本机实测拟合的外观模型，给着色对比度区间现算用。
 *
 * 系统亚克力 = 背后内容的**明度**拉向一个灰、色相与饱和度留着（D2D `D2D1_BLEND_MODE_LUMINOSITY` 那一步）；
 * 云母把明度整个换成底色，再盖一层同色。`recipe()` 的参数是对着激活态的系统材质量出来的（数据见 .cpp），
 * `composite()` 把它作用在一个背后颜色上。窗口失焦时 WindowChrome 让 DWM 继续画激活态（见 WindowChrome.hpp
 * 【失焦不退色】），所以这就是用户任何时候看到的那层底。
 *
 * 不依赖 Qt 与 Windows 头，用例与验证小样可以直接拿它编。
 */

#pragma once

namespace SystemBackdrop {

enum class Material {
    Acrylic,  ///< DWMSBT_TRANSIENTWINDOW：背后的桌面与窗口
    Mica,     ///< DWMSBT_MAINWINDOW：只取壁纸
};

/// 背后内容先做一次「明度混合」——明度拉向 `luminosity` 的颜色、拉多少 = 它的 alpha，色相与饱和度留着；
/// 再盖一层普通半透明色 `overlay`（alpha 0 = 不盖）。颜色都是 0xAARRGGBB。
struct Recipe {
    unsigned luminosity = 0;
    unsigned overlay = 0;
};

/// 照系统材质拟合出来的配方（本机 26200 对着纯黑 / 纯白 / 棋盘量的）。
Recipe recipe(Material material, bool dark);
/// 配方作用在一个不透明的背后颜色上的结果（0xRRGGBB → 0xRRGGBB，W3C 的 SetLum / ClipColor）。
unsigned composite(const Recipe &recipe, unsigned rgb);

}  // namespace SystemBackdrop
