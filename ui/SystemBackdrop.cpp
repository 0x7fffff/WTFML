#include "SystemBackdrop.hpp"

#include <algorithm>

namespace SystemBackdrop {

// 同一个窗口、背后分别垫纯黑 / 纯白 / 红蓝棋盘，量激活态的平均色（2026-11-12，26200）：
// - 亚克力浅色：黑 → #878787、白 → #e3e3e3，斜率 0.36 ⇒ 明度拉向 #d3d3d3、强度 0.64（#d3d3d3 正是 DWM 失焦时换上的那块平灰）；
//   按这组参数推棋盘 #7f007f 得 (209,115,209)，实测 #cd75cd。
// - 亚克力深色：黑 → #363636、白 → #929292 ⇒ 拉向 #545454、强度 0.64；推棋盘 (147,20,147)，实测 #931493。
// - 云母：明度整个换成底色（强度 1.0），再盖一层 50%（浅）/ 80%（深）的同色；浅色推壁纸那一块 (249,240,240)，实测 #f8f0f0。
Recipe recipe(Material material, bool dark) {
    if (material == Material::Mica) return dark ? Recipe{0xff202020u, 0xcc202020u} : Recipe{0xfff3f3f3u, 0x80f3f3f3u};
    return dark ? Recipe{0xa3545454u, 0u} : Recipe{0xa3d3d3d3u, 0u};
}

namespace {

struct Rgb {
    double r, g, b;
};

double lum(const Rgb &c) { return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b; }

Rgb clipColor(Rgb c) {
    const double l = lum(c);
    const double n = std::min({c.r, c.g, c.b});
    const double x = std::max({c.r, c.g, c.b});
    const auto scale = [&](double k) { c = {l + (c.r - l) * k, l + (c.g - l) * k, l + (c.b - l) * k}; };
    if (n < 0.0) scale(l / (l - n));
    if (x > 1.0) scale((1.0 - l) / (x - l));
    return c;
}

Rgb setLum(const Rgb &c, double l) {
    const double d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}

Rgb unpack(unsigned rgb) { return {((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0}; }
double alphaOf(unsigned argb) { return ((argb >> 24) & 0xff) / 255.0; }
Rgb mix(const Rgb &top, const Rgb &bottom, double a) {
    return {top.r * a + bottom.r * (1 - a), top.g * a + bottom.g * (1 - a), top.b * a + bottom.b * (1 - a)};
}

}  // namespace

unsigned composite(const Recipe &recipe, unsigned rgb) {
    const Rgb back = unpack(rgb);
    const Rgb lifted = setLum(back, lum(unpack(recipe.luminosity)));
    const Rgb out = mix(unpack(recipe.overlay), mix(lifted, back, alphaOf(recipe.luminosity)), alphaOf(recipe.overlay));
    const auto channel = [](double v) { return unsigned(std::clamp(v * 255.0 + 0.5, 0.0, 255.0)); };
    return (channel(out.r) << 16) | (channel(out.g) << 8) | channel(out.b);
}

}  // namespace SystemBackdrop
