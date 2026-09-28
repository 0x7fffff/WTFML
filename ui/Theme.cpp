/**
 * @file Theme.cpp
 * @brief Theme 的实现：预设 / 覆盖项怎么算成一个调色板，以及"地方"和"语义色"现在是什么颜色。
 * @author 0x7fffff
 * @date 2026-09-28
 */

#include "Theme.hpp"

#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

namespace Theme {
namespace {

// ---------------------------------------------------------------------------
// 那几张表：每一项都是"画面上哪儿"的唯一出处
// ---------------------------------------------------------------------------

/// 一块骨架今天的样子：`from` 那一档颜色压暗 `shade`，
///
///   - `shade == 0` ⇒ 直接用 `from` 那一档（卡片：今天就是 `Base`）；
///   - `literal` ⇒ 默认值是个写死的色（四周变暗那一层），不从 `from` 推。
struct PartInfo {
    const char *key;
    const char *name;
    int shade;
    QPalette::ColorRole from;
    bool literal;
};

/// 顺序 = `Part` 的枚举顺序。`static_assert` 在下面盯着。
const PartInfo kPartInfos[] = {
    {"titleBar", "标题栏", 105, QPalette::Window, false},
    {"bottomBar", "底栏", 105, QPalette::Window, false},
    {"panel", "浮层面板", 112, QPalette::Window, false},
    {"card", "卡片", 0, QPalette::Base, false},
    {"scrim", "四周变暗", 0, QPalette::Window, true},
};
static_assert(sizeof(kPartInfos) / sizeof(kPartInfos[0]) == static_cast<size_t>(partCount()),
              "kPartInfos 和 Part 的项数对不上 —— 加一块骨架时两处都要动");

/// 四周变暗那一层的出厂色。**和 `OverlayHost::scrimColor()` 从前的字面量一字不差**：
/// 半透明黑，盖在什么底色上都成立。
const QColor &scrimLiteral() {
    static const QColor color(0, 0, 0, 96);
    return color;
}

/// 能单独改的调色板角色表（顺序 = `Role` 的枚举顺序）。
struct RoleInfo {
    const char *key;
    const char *name;
    QPalette::ColorRole role;
};

const RoleInfo kRoleInfos[] = {
    {"window", "内容区底色", QPalette::Window},
    {"base", "输入框/列表底色", QPalette::Base},
    {"alternateBase", "隔行底色", QPalette::AlternateBase},
    {"text", "输入框/列表里的字", QPalette::Text},
    {"windowText", "正文", QPalette::WindowText},
    {"button", "按钮底色", QPalette::Button},
    {"buttonText", "按钮上的字", QPalette::ButtonText},
    {"highlight", "选中/悬停", QPalette::Highlight},
    {"highlightedText", "选中那一块上的字", QPalette::HighlightedText},
    {"placeholderText", "次要文字", QPalette::PlaceholderText},
    {"mid", "分隔线", QPalette::Mid},
};
static_assert(sizeof(kRoleInfos) / sizeof(kRoleInfos[0]) == static_cast<size_t>(roleCount()),
              "kRoleInfos 和 Role 的项数对不上 —— 加一个角色时两处都要动");

/// 文字不透明度落在哪几个角色上。**只有带字的角色**：`Highlight` 那种是"一块底"，
/// 跟着变淡的话选中那一块就淡了，那不是"文字透明度"。
///
/// NOTE: `ToolTipText` 也在这儿，它**不在** `kRoleInfos` 里（用户调不到它，但它是字，
///       全局那一位得管它，不然提示框里的字会比别处清楚）。
const QPalette::ColorRole kInkRoles[] = {
    QPalette::WindowText, QPalette::Text,        QPalette::ButtonText,
    QPalette::HighlightedText, QPalette::PlaceholderText, QPalette::ToolTipText,
};

QPalette::ColorGroup colorGroups[] = {QPalette::Active, QPalette::Inactive, QPalette::Disabled};

// ---------------------------------------------------------------------------
// 全局那一份
// ---------------------------------------------------------------------------

Spec &mutableSpec() {
    static Spec spec = defaultSpec();
    return spec;
}

/// 上一次 `apply()` 有没有往应用上调色板铺过东西（不是出厂那一份）。
///
/// 要它是因为"跟着系统"那一档得能**收回**：铺过深色预设之后用户又选回"跟着系统"，
/// 光"什么都不做"是收敛不回去的（深色那一份还挂在应用上）。
bool &installedExplicit() {
    static bool installed = false;
    return installed;
}

/// 我们**顶掉**的那一份调色板 = 平台自己给的那一份。
///
/// ★ 要它是因为"跟着系统"那一档得能**原样**收回来，而 `QStyle::standardPalette()` **不是**
///   平台那一份：真机实测（2026-09-28）平台给的是 `Window = #f0f0f0`，Fusion 的
///   `standardPalette()` 是 `#efefef` —— 于是"深色 → 跟着系统"之后整个窗口都差着一档。
///   `ui_shot` 的 30) 是拿"换主题之前那一张"逐像素比出来的：**还有 141 万个像素停在别的
///   颜色上**（那是 2026-09-28 修之前量的数），而界面上人眼是看不出来的 —— 典型的"改回来
///   了、但没改回原样"。所以这儿把顶掉的那一份存下来，还原时用它，不用算出来的那一份。
///
/// NOTE: Qt 没有"把调色板还回去"这个接口（`setPalette()` 是单向的），所以只能自己记。
/// NOTE: 记的是"还没顶掉过的时候应用身上挂着的那一份" —— 那一刻它就是平台给的
///       （`apply()` 每次都会顺手刷新，见那儿的注释）。
QPalette &platformPalette() {
    static QPalette palette;
    return palette;
}

bool &platformPaletteKnown() {
    static bool known = false;
    return known;
}

int clampOpacity(int value) {
    return qBound(0, value, 100);
}

/// 调色板里"正文/次要文字从哪儿取"那两档的当前值（`partForeground()` 的兜底用）。
QColor appColor(QPalette::ColorRole role) {
    if (QGuiApplication::instance() == nullptr) {
        return QColor();
    }
    return QGuiApplication::palette().color(role);
}

// ---------------------------------------------------------------------------
// 存盘的键
// ---------------------------------------------------------------------------

const char *const kPresetKey = "Theme/Preset";
const char *const kTextOpacityKey = "Theme/TextOpacity";
const char *const kRolesPrefix = "Theme/Roles/";
const char *const kPartsPrefix = "Theme/Parts/";
const char *const kAccentsPrefix = "Theme/Accents/";

/// 色存成 `#aarrggbb`（`QColor::HexArgb`）—— 注册表里能手看、能手改，和别的设置一个待遇。
QString colorToString(const QColor &color) {
    return color.isValid() ? color.name(QColor::HexArgb) : QString();
}

QColor colorFromString(const QString &text) {
    const QColor color = QColor::fromString(text);
    // 认不出的字符串（手改歪了）当"没设"：`fromString()` 对垃圾给一个无效色。
    return color.isValid() ? color : QColor();
}

QString roleSettingKey(Role role) {
    return QLatin1String(kRolesPrefix) + QLatin1String(kRoleInfos[static_cast<int>(role)].key);
}

QString partSettingKey(Part part, const char *field) {
    return QLatin1String(kPartsPrefix) + QLatin1String(kPartInfos[static_cast<int>(part)].key) +
           QLatin1Char('/') + QLatin1String(field);
}

QString accentSettingKey(const char *field) {
    return QLatin1String(kAccentsPrefix) + QLatin1String(field);
}

}  // namespace

// ---------------------------------------------------------------------------
// 名字 / 键
// ---------------------------------------------------------------------------

QString partKey(Part part) {
    const int index = static_cast<int>(part);
    return index >= 0 && index < partCount() ? QLatin1String(kPartInfos[index].key) : QString();
}

QString partName(Part part) {
    const int index = static_cast<int>(part);
    return index >= 0 && index < partCount() ? QString::fromUtf8(kPartInfos[index].name) : QString();
}

bool partFromKey(const QString &key, Part *out) {
    for (int i = 0; i < partCount(); ++i) {
        if (key == QLatin1String(kPartInfos[i].key)) {
            if (out != nullptr) {
                *out = static_cast<Part>(i);
            }
            return true;
        }
    }
    return false;
}

QPalette::ColorRole qtRole(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? kRoleInfos[index].role : QPalette::WindowText;
}

QString roleKey(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? QLatin1String(kRoleInfos[index].key) : QString();
}

QString roleName(Role role) {
    const int index = static_cast<int>(role);
    return index >= 0 && index < roleCount() ? QString::fromUtf8(kRoleInfos[index].name) : QString();
}

bool roleFromKey(const QString &key, Role *out) {
    for (int i = 0; i < roleCount(); ++i) {
        if (key == QLatin1String(kRoleInfos[i].key)) {
            if (out != nullptr) {
                *out = static_cast<Role>(i);
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 预设
// ---------------------------------------------------------------------------

QStringList presetIds() {
    return {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")};
}

QString presetName(const QString &id) {
    if (id == QLatin1String("light")) {
        return QStringLiteral("浅色");
    }
    if (id == QLatin1String("dark")) {
        return QStringLiteral("深色");
    }
    return QStringLiteral("跟着系统");
}

bool isPresetId(const QString &id) {
    return presetIds().contains(id);
}

Spec presetSpec(const QString &id) {
    Spec spec;
    spec.presetId = isPresetId(id) ? id : defaultSpec().presetId;
    return spec;
}

Spec defaultSpec() {
    // 出厂那一份**故意什么都没有**：跟系统、不透明度 100、一项覆盖都没有 ⇒ `apply()`
    // 一个字节都不碰（见文件头那段"默认那一份 = 今天的样子"）。
    Spec spec;
    spec.presetId = QStringLiteral("system");
    return spec;
}

// ---------------------------------------------------------------------------
// 当前这一份
// ---------------------------------------------------------------------------

Spec normalize(const Spec &spec) {
    Spec clean;
    clean.presetId = isPresetId(spec.presetId) ? spec.presetId : defaultSpec().presetId;
    clean.textOpacity = clampOpacity(spec.textOpacity);
    for (auto it = spec.roles.constBegin(); it != spec.roles.constEnd(); ++it) {
        // 越界的下标（盘上手改出来的）直接丢：认不出的角色没有"画面上是哪儿"可言。
        if (it.key() < 0 || it.key() >= roleCount() || !it.value().isValid()) {
            continue;
        }
        clean.roles.insert(it.key(), it.value());
    }
    for (auto it = spec.parts.constBegin(); it != spec.parts.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= partCount()) {
            continue;
        }
        PartStyle style = it.value();
        style.hasBackground = style.hasBackground && style.background.isValid();
        style.hasForeground = style.hasForeground && style.foreground.isValid();
        style.textOpacity = style.textOpacity < 0 ? -1 : clampOpacity(style.textOpacity);
        // 一项覆盖都没有的那一块**不留**：留着的话"这一块设过"这件事在盘上是假的
        // （读回来之后 `parts.contains()` 说它有，而它什么都没改）。
        if (!style.hasBackground && !style.hasForeground && style.textOpacity < 0) {
            continue;
        }
        clean.parts.insert(it.key(), style);
    }
    clean.danger = spec.danger.isValid() ? spec.danger : QColor();
    clean.warning = spec.warning.isValid() ? spec.warning : QColor();
    clean.ok = spec.ok.isValid() ? spec.ok : QColor();
    clean.errorText = spec.errorText.isValid() ? spec.errorText : QColor();
    return clean;
}

const Spec &current() {
    return mutableSpec();
}

void setSpec(const Spec &spec) {
    mutableSpec() = normalize(spec);
}

// ---------------------------------------------------------------------------
// 铺开
// ---------------------------------------------------------------------------

bool isDarkPalette(const QPalette &palette) {
    // 判据取的是**窗口底色的明度**（不是某个"主题叫什么"的标志位）：深底用浅红、
    // 浅底用深红，两种主题下都是"一眼看出是红字"而不是"一团糊在底色里"。
    return palette.color(QPalette::Window).lightness() < 128;
}

bool isDark() {
    return isDarkPalette(QGuiApplication::palette());
}

QPalette buildPalette(const Spec &spec, const QPalette &base) {
    QPalette palette = base;
    for (auto it = spec.roles.constBegin(); it != spec.roles.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= roleCount() || !it.value().isValid()) {
            continue;
        }
        palette.setColor(qtRole(static_cast<Role>(it.key())), it.value());
    }
    if (spec.textOpacity != 100) {
        for (QPalette::ColorRole role : kInkRoles) {
            for (QPalette::ColorGroup group : colorGroups) {
                QColor color = palette.color(group, role);
                if (!color.isValid()) {
                    continue;
                }
                // **乘**上去而不是盖掉：`PlaceholderText` 本来就带一点透明度，
                // 直接赋值等于把它从半透明变成"和正文一样清楚"。
                color.setAlpha(color.alpha() * spec.textOpacity / 100);
                palette.setColor(group, role, color);
            }
        }
    }
    return palette;
}

void notifyWidgets() {
    if (QGuiApplication::instance() == nullptr) {
        return;
    }
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        QEvent event(QEvent::PaletteChange);
        QCoreApplication::sendEvent(widget, &event);
    }
}

void apply(QApplication &app) {
    const Spec &spec = current();
    const bool followSystem = spec.presetId == QLatin1String("system");

    // 0) 记下"平台自己那一份"。`installedExplicit()` 为假 = 应用身上现在挂的就是平台给的
    //    那一份（我们没顶掉过，或者已经还回去了）—— 这一刻存下来，还原时才有东西可还。
    //    顶掉过的那一档不刷新：那时候应用身上挂着的是**我们**铺的，存它等于把深色当成了
    //    "平台那一份"（见 `platformPalette()` 那段）。
    if (!installedExplicit()) {
        platformPalette() = app.palette();
        platformPaletteKnown() = true;
    }

    // 1) 深浅那一档。**只有"跟着系统"那一档才不去动它**（见头文件那段 NOTE）。
    if (followSystem) {
        app.styleHints()->unsetColorScheme();
    } else {
        app.styleHints()->setColorScheme(spec.presetId == QLatin1String("dark")
                                             ? Qt::ColorScheme::Dark
                                             : Qt::ColorScheme::Light);
    }

    // 2) 基色一律现问样式要。**不能拿"现在的应用调色板"当基色**：铺过一次深色预设之后
    //    它就是深色那一份了，再叠一次"跟系统"的覆盖项会得到深底 + 浅色覆盖的怪东西。
    //    这一刻深浅那一档已经定好，所以 standardPalette() 问到的就是那一档。
    const QPalette base = app.style()->standardPalette();

    const bool touchesPalette = !followSystem || !spec.roles.isEmpty() || spec.textOpacity != 100;
    if (!touchesPalette) {
        // 出厂那一份：今天的样子。**一个字节都不碰** —— 碰了就等于把"跟着系统"钉死
        // （Qt 只在"调色板不是我们设的"时才跟着系统深浅走）。
        if (installedExplicit()) {
            // 例外：上一次铺过别的预设，这一次要选回"跟着系统" ⇒ 得把出厂那一份盖回去，
            // 光"什么都不做"是收不回的（深色那份还挂在应用上）。
            // ★ 还回去的是**记下来的平台那一份**，不是上面算出来的 `base`：`base` 是
            //   `standardPalette()`，和平台那一份差着一档（见 `platformPalette()` 那段）。
            //   没记下来过（理论上到不了）才退而求其次用 `base`。
            app.setPalette(platformPaletteKnown() ? platformPalette() : base);
            installedExplicit() = false;
        }
        notifyWidgets();
        return;
    }

    app.setPalette(buildPalette(spec, base));
    installedExplicit() = true;
    // 铺完之后 Qt 自己会把"调色板变了"发给每一个控件（`ui_check` W2 量着这件事），
    // 所以这里**不再**敲第二遍 —— 敲了就是每换一次主题白跑一趟全窗口的重算。
}

// ---------------------------------------------------------------------------
// 每一块 / 每一个语义色
// ---------------------------------------------------------------------------

QColor partBackground(Part part) {
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return QColor();
    }
    const auto it = current().parts.constFind(index);
    if (it != current().parts.constEnd() && it->hasBackground) {
        return it->background;
    }
    const PartInfo &info = kPartInfos[index];
    if (info.literal) {
        return scrimLiteral();
    }
    const QColor from = appColor(info.from);
    return info.shade == 0 ? from : from.darker(info.shade);
}

QColor partForeground(Part part, const QColor &fallback) {
    const int index = static_cast<int>(part);
    if (index < 0 || index >= partCount()) {
        return fallback;
    }
    const auto it = current().parts.constFind(index);
    if (it == current().parts.constEnd()) {
        return fallback;
    }
    QColor color = it->hasForeground ? it->foreground : fallback;
    // 这一块**自己**那一位才在这儿生效；全局那一位落在调色板的文字角色上（`buildPalette()`），
    // 而 `fallback` 多半就是从调色板取来的 ⇒ 在这儿再乘一次就是乘了两遍。
    if (it->textOpacity >= 0 && color.isValid()) {
        color.setAlpha(color.alpha() * it->textOpacity / 100);
    }
    return color;
}

QColor scrimColor() {
    return partBackground(Part::Scrim);
}

namespace {

/// 语义色的出厂值：一深一浅两档。**参数是"算不算深色"**，不是调色板 —— 三个语义色
/// 都只用得到这一位。
QColor dangerFor(bool dark) {
    return dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xc0, 0x28, 0x28);
}

}  // namespace

QColor dangerColor(const QPalette &palette) {
    const QColor override = current().danger;
    if (override.isValid()) {
        return override;
    }
    return dangerFor(isDarkPalette(palette));
}

QColor warningColor(const QPalette &palette) {
    const QColor override = current().warning;
    if (override.isValid()) {
        return override;
    }
    // 黄这一档取的是**偏金黄**的琥珀色（不是纯黄 #ffff00）：那块方块的底色是底栏，
    // 纯黄在浅色底上几乎看不见。色相实测 43(浅)/45(深)，和红那一档（0）差得开。
    return isDarkPalette(palette) ? QColor(0xff, 0xd2, 0x4d) : QColor(0xe0, 0xa0, 0x00);
}

QColor okColor(const QPalette &palette) {
    const QColor override = current().ok;
    if (override.isValid()) {
        return override;
    }
    return isDarkPalette(palette) ? QColor(0x5e, 0xd1, 0x7a) : QColor(0x1f, 0x9d, 0x4d);
}

QColor errorTextColor(const QPalette &palette) {
    const QColor override = current().errorText;
    if (override.isValid()) {
        return override;
    }
    // 浅底那一档和原来那个写死的 #c0392b **一字不差**（对浅色主题的窗口底色 #f0f0f0
    // 实测 4.77:1，够 WCAG 正文那条 4.5:1）。深底那一档没法照搬：同一个 #c0392b 落到
    // 深色主题的底上只有 2.26:1 —— 名字写错了却看不出底下那行写了什么，等于没有。
    // 所以借用同一个界面里已经调好的那个浅红（`dangerColor()`）。
    // NOTE: 两处红**故意**不合并成一个语义色：浅底那一档 #c0392b 和 #c02828 各有各的
    //       对比度来历（见 `MainWindowUI::installNameErrorColor()` 从前那段注释）。
    return isDarkPalette(palette) ? dangerFor(true) : QColor(0xc0, 0x39, 0x2b);
}

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------

void save(const Spec &spec) {
    const Spec clean = normalize(spec);
    QSettings settings;
    // 先整组清掉再写：上一次设过、这一次没设的那几项要是留着，读回来就会"复活"
    // （用户把标题栏改回默认了，重开一次又变回去）。
    settings.remove(QStringLiteral("Theme"));
    settings.setValue(QLatin1String(kPresetKey), clean.presetId);
    settings.setValue(QLatin1String(kTextOpacityKey), clean.textOpacity);
    for (auto it = clean.roles.constBegin(); it != clean.roles.constEnd(); ++it) {
        settings.setValue(roleSettingKey(static_cast<Role>(it.key())), colorToString(it.value()));
    }
    for (auto it = clean.parts.constBegin(); it != clean.parts.constEnd(); ++it) {
        const PartStyle &style = it.value();
        const Part part = static_cast<Part>(it.key());
        if (style.hasBackground) {
            settings.setValue(partSettingKey(part, "Background"), colorToString(style.background));
        }
        if (style.hasForeground) {
            settings.setValue(partSettingKey(part, "Foreground"), colorToString(style.foreground));
        }
        if (style.textOpacity >= 0) {
            settings.setValue(partSettingKey(part, "TextOpacity"), style.textOpacity);
        }
    }
    if (clean.danger.isValid()) {
        settings.setValue(accentSettingKey("Danger"), colorToString(clean.danger));
    }
    if (clean.warning.isValid()) {
        settings.setValue(accentSettingKey("Warning"), colorToString(clean.warning));
    }
    if (clean.ok.isValid()) {
        settings.setValue(accentSettingKey("Ok"), colorToString(clean.ok));
    }
    if (clean.errorText.isValid()) {
        settings.setValue(accentSettingKey("ErrorText"), colorToString(clean.errorText));
    }
    settings.sync();
}

Spec load() {
    QSettings settings;
    Spec spec = defaultSpec();
    spec.presetId = settings.value(QLatin1String(kPresetKey), spec.presetId).toString();
    spec.textOpacity = settings.value(QLatin1String(kTextOpacityKey), spec.textOpacity).toInt();
    for (int i = 0; i < roleCount(); ++i) {
        const Role role = static_cast<Role>(i);
        const QColor color = colorFromString(
            settings.value(roleSettingKey(role)).toString());
        if (color.isValid()) {
            spec.roles.insert(i, color);
        }
    }
    for (int i = 0; i < partCount(); ++i) {
        const Part part = static_cast<Part>(i);
        PartStyle style;
        style.background = colorFromString(
            settings.value(partSettingKey(part, "Background")).toString());
        style.hasBackground = style.background.isValid();
        style.foreground = colorFromString(
            settings.value(partSettingKey(part, "Foreground")).toString());
        style.hasForeground = style.foreground.isValid();
        const QVariant opacity = settings.value(partSettingKey(part, "TextOpacity"));
        if (opacity.isValid()) {
            style.textOpacity = opacity.toInt();
        }
        if (style.hasBackground || style.hasForeground || style.textOpacity >= 0) {
            spec.parts.insert(i, style);
        }
    }
    spec.danger = colorFromString(settings.value(accentSettingKey("Danger")).toString());
    spec.warning = colorFromString(settings.value(accentSettingKey("Warning")).toString());
    spec.ok = colorFromString(settings.value(accentSettingKey("Ok")).toString());
    spec.errorText = colorFromString(settings.value(accentSettingKey("ErrorText")).toString());
    // 盘上那一份是能手改的、也可能是以后降级回退留下的：**认不出的一律退回出厂**，
    // 不猜也不报错（和 LauncherSettings::load() 那几处同一个路数）。
    return normalize(spec);
}

// ---------------------------------------------------------------------------
// follow()
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief "主题一变就重算一遍"这条登记。
 *
 * 它是个事件过滤器而不是"调色板变了"这个信号 —— Qt 没有那样的信号，但**每个控件**都会
 * 收到 `ApplicationPaletteChange`（应用级）和 `PaletteChange`（控件级）。
 */
class PaletteFollower : public QObject {
public:
    PaletteFollower(QWidget *owner, std::function<void()> fn)
        : QObject(owner), m_fn(std::move(fn)) {
        owner->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        switch (event->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            // ★ 防自咬：`m_fn()` 里多半就是 `setStyleSheet()` / `setPalette()`，而这两样
            //   各自会**再发一次** `StyleChange` / `PaletteChange` —— 不拦的话就是
            //   "重算 → 又收到 → 再重算"，而且它是无限递归，屏幕上只会看到卡死。
            //   `ui_check` W6 那条断言就是照着这句话量的（计数器不许涨第二下）。
            if (!m_running) {
                m_running = true;
                m_fn();
                m_running = false;
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_fn;
    bool m_running = false;
};

}  // namespace

void followImpl(QWidget *owner, std::function<void()> fn) {
    // parent 就是 owner ⇒ owner 没了这一条自己掉，不用谁来收。
    new PaletteFollower(owner, std::move(fn));
}

}  // namespace Theme
