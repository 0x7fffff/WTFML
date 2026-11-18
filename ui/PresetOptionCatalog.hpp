#pragma once
#include <QString>
#include <QCoreApplication>
#include <QStringList>
#include <QList>

namespace PresetOptionCatalog {
enum class Kind { Boolean, Integer, Fraction, Choice, Key };
struct Option {
    QString key, name, group, initial;
    Kind kind;
    double minimum = 0, maximum = 1;
    QStringList choices = {};
};
inline const QList<Option> &all() {
    static const QList<Option> options = [] {
        QList<Option> result;
        auto add = [&](const char *key, const QString &name, const QString &group, const char *initial,
                       Kind kind, double low = 0, double high = 1, QStringList choices = {}) {
            result.append({QString::fromLatin1(key), name, group,
                           QString::fromLatin1(initial), kind, low, high, choices});
        };
        add("fov", QCoreApplication::translate("PresetOptionCatalog", "视角"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "0.0", Kind::Integer, 30, 110);
        add("guiScale", QCoreApplication::translate("PresetOptionCatalog", "界面尺寸"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "0", Kind::Integer, 0, 16);
        add("renderDistance", QCoreApplication::translate("PresetOptionCatalog", "渲染距离"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "12", Kind::Integer, 2, 64);
        add("simulationDistance", QCoreApplication::translate("PresetOptionCatalog", "模拟距离"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "12", Kind::Integer, 2, 32);
        add("maxFps", QCoreApplication::translate("PresetOptionCatalog", "帧率上限"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "120", Kind::Integer, 10, 260);
        add("gamma", QCoreApplication::translate("PresetOptionCatalog", "亮度"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "0.5", Kind::Fraction);
        add("enableVsync", QCoreApplication::translate("PresetOptionCatalog", "垂直同步"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "true", Kind::Boolean);
        add("fullscreen", QCoreApplication::translate("PresetOptionCatalog", "全屏"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "false", Kind::Boolean);
        add("graphicsMode", QCoreApplication::translate("PresetOptionCatalog", "画质"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "1", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "0|流畅"), QCoreApplication::translate("PresetOptionCatalog", "1|高品质"), QCoreApplication::translate("PresetOptionCatalog", "2|极佳")});
        add("particles", QCoreApplication::translate("PresetOptionCatalog", "粒子效果"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "0", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "0|全部"), QCoreApplication::translate("PresetOptionCatalog", "1|减少"), QCoreApplication::translate("PresetOptionCatalog", "2|最少")});
        add("renderClouds", QCoreApplication::translate("PresetOptionCatalog", "云"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "true", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "false|关闭"), QCoreApplication::translate("PresetOptionCatalog", "fast|快速"), QCoreApplication::translate("PresetOptionCatalog", "true|精细")});
        add("entityShadows", QCoreApplication::translate("PresetOptionCatalog", "实体阴影"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "true", Kind::Boolean);
        add("bobView", QCoreApplication::translate("PresetOptionCatalog", "视角摇晃"), QCoreApplication::translate("PresetOptionCatalog", "画面"), "true", Kind::Boolean);
        add("autoJump", QCoreApplication::translate("PresetOptionCatalog", "自动跳跃"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "false", Kind::Boolean);
        add("mouseSensitivity", QCoreApplication::translate("PresetOptionCatalog", "鼠标灵敏度"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "0.5", Kind::Fraction);
        add("invertYMouse", QCoreApplication::translate("PresetOptionCatalog", "反转鼠标"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "false", Kind::Boolean);
        add("rawMouseInput", QCoreApplication::translate("PresetOptionCatalog", "原始鼠标输入"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "true", Kind::Boolean);
        add("toggleCrouch", QCoreApplication::translate("PresetOptionCatalog", "潜行方式"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "false", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "false|按住"), QCoreApplication::translate("PresetOptionCatalog", "true|切换")});
        add("toggleSprint", QCoreApplication::translate("PresetOptionCatalog", "疾跑方式"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "false", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "false|按住"), QCoreApplication::translate("PresetOptionCatalog", "true|切换")});
        add("mainHand", QCoreApplication::translate("PresetOptionCatalog", "主手"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "right", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "right|右手"), QCoreApplication::translate("PresetOptionCatalog", "left|左手")});
        add("pauseOnLostFocus", QCoreApplication::translate("PresetOptionCatalog", "失去焦点时暂停"), QCoreApplication::translate("PresetOptionCatalog", "操作"), "true", Kind::Boolean);
        add("showSubtitles", QCoreApplication::translate("PresetOptionCatalog", "字幕"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "false", Kind::Boolean);
        add("highContrast", QCoreApplication::translate("PresetOptionCatalog", "高对比度"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "false", Kind::Boolean);
        add("hideLightningFlash", QCoreApplication::translate("PresetOptionCatalog", "隐藏闪电闪光"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "false", Kind::Boolean);
        add("screenEffectScale", QCoreApplication::translate("PresetOptionCatalog", "屏幕扭曲效果"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "1.0", Kind::Fraction);
        add("fovEffectScale", QCoreApplication::translate("PresetOptionCatalog", "视角变化效果"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "1.0", Kind::Fraction);
        add("darknessEffectScale", QCoreApplication::translate("PresetOptionCatalog", "黑暗效果"), QCoreApplication::translate("PresetOptionCatalog", "辅助功能"), "1.0", Kind::Fraction);
        add("chatVisibility", QCoreApplication::translate("PresetOptionCatalog", "聊天显示"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "0", Kind::Choice, 0, 1, {QCoreApplication::translate("PresetOptionCatalog", "0|显示"), QCoreApplication::translate("PresetOptionCatalog", "1|仅命令"), QCoreApplication::translate("PresetOptionCatalog", "2|隐藏")});
        add("chatColors", QCoreApplication::translate("PresetOptionCatalog", "聊天颜色"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "true", Kind::Boolean);
        add("chatLinks", QCoreApplication::translate("PresetOptionCatalog", "聊天链接"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "true", Kind::Boolean);
        add("chatLinksPrompt", QCoreApplication::translate("PresetOptionCatalog", "打开链接前确认"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "true", Kind::Boolean);
        add("chatOpacity", QCoreApplication::translate("PresetOptionCatalog", "聊天不透明度"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "1.0", Kind::Fraction);
        add("chatScale", QCoreApplication::translate("PresetOptionCatalog", "聊天缩放"), QCoreApplication::translate("PresetOptionCatalog", "聊天"), "1.0", Kind::Fraction);
        const QStringList sounds{QCoreApplication::translate("PresetOptionCatalog", "master|主音量"), QCoreApplication::translate("PresetOptionCatalog", "music|音乐"), QCoreApplication::translate("PresetOptionCatalog", "record|唱片"), QCoreApplication::translate("PresetOptionCatalog", "weather|天气"), QCoreApplication::translate("PresetOptionCatalog", "block|方块"),
                                 QCoreApplication::translate("PresetOptionCatalog", "hostile|敌对生物"), QCoreApplication::translate("PresetOptionCatalog", "neutral|友好生物"), QCoreApplication::translate("PresetOptionCatalog", "player|玩家"), QCoreApplication::translate("PresetOptionCatalog", "ambient|环境"), QCoreApplication::translate("PresetOptionCatalog", "voice|语音")};
        for (const auto &sound : sounds) {
            auto pair = sound.split('|');
            result.append({"soundCategory_" + pair[0], pair[1], QCoreApplication::translate("PresetOptionCatalog", "声音"), "1.0", Kind::Fraction});
        }
        const QStringList keys{
            QCoreApplication::translate("PresetOptionCatalog", "forward|前进|w"), QCoreApplication::translate("PresetOptionCatalog", "back|后退|s"), QCoreApplication::translate("PresetOptionCatalog", "left|左移|a"), QCoreApplication::translate("PresetOptionCatalog", "right|右移|d"), QCoreApplication::translate("PresetOptionCatalog", "jump|跳跃|space"),
            QCoreApplication::translate("PresetOptionCatalog", "sneak|潜行|left.shift"), QCoreApplication::translate("PresetOptionCatalog", "sprint|疾跑|left.control"), QCoreApplication::translate("PresetOptionCatalog", "inventory|物品栏|e"), QCoreApplication::translate("PresetOptionCatalog", "drop|丢弃物品|q"),
            QCoreApplication::translate("PresetOptionCatalog", "swapOffhand|交换副手物品|f"), QCoreApplication::translate("PresetOptionCatalog", "chat|打开聊天|t"), QCoreApplication::translate("PresetOptionCatalog", "command|输入命令|slash"),
            QCoreApplication::translate("PresetOptionCatalog", "playerlist|玩家列表|tab"), QCoreApplication::translate("PresetOptionCatalog", "togglePerspective|切换视角|f5"), QCoreApplication::translate("PresetOptionCatalog", "screenshot|截图|f2"),
            QCoreApplication::translate("PresetOptionCatalog", "fullscreen|切换全屏|f11"), QCoreApplication::translate("PresetOptionCatalog", "advancements|进度|l")};
        for (const auto &key : keys) {
            auto pair = key.split('|');
            result.append({"key_key." + pair[0], pair[1], QCoreApplication::translate("PresetOptionCatalog", "按键"), "key.keyboard." + pair[2], Kind::Key});
        }
        result.append({"key_key.attack", QCoreApplication::translate("PresetOptionCatalog", "攻击 / 破坏"), QCoreApplication::translate("PresetOptionCatalog", "按键"), "key.mouse.left", Kind::Key});
        result.append({"key_key.use", QCoreApplication::translate("PresetOptionCatalog", "使用 / 放置"), QCoreApplication::translate("PresetOptionCatalog", "按键"), "key.mouse.right", Kind::Key});
        result.append({"key_key.pickItem", QCoreApplication::translate("PresetOptionCatalog", "选取方块"), QCoreApplication::translate("PresetOptionCatalog", "按键"), "key.mouse.middle", Kind::Key});
        for (int i = 1; i <= 9; ++i)
            result.append({"key_key.hotbar." + QString::number(i), QCoreApplication::translate("PresetOptionCatalog", "快捷栏 ") + QString::number(i),
                           QCoreApplication::translate("PresetOptionCatalog", "按键"), "key.keyboard." + QString::number(i), Kind::Key});
        return result;
    }();
    return options;
}
inline const Option *find(const QString &key) {
    for (const auto &option : all()) if (option.key == key) return &option;
    return nullptr;
}
}

