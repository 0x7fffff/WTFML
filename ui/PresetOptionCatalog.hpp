#pragma once
#include <QString>
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
        auto add = [&](const char *key, const char *name, const char *group, const char *initial,
                       Kind kind, double low = 0, double high = 1, QStringList choices = {}) {
            result.append({QString::fromLatin1(key), QString::fromUtf8(name), QString::fromUtf8(group),
                           QString::fromLatin1(initial), kind, low, high, choices});
        };
        add("fov", "视角", "画面", "0.0", Kind::Integer, 30, 110);
        add("guiScale", "界面尺寸", "画面", "0", Kind::Integer, 0, 16);
        add("renderDistance", "渲染距离", "画面", "12", Kind::Integer, 2, 64);
        add("simulationDistance", "模拟距离", "画面", "12", Kind::Integer, 2, 32);
        add("maxFps", "帧率上限", "画面", "120", Kind::Integer, 10, 260);
        add("gamma", "亮度", "画面", "0.5", Kind::Fraction);
        add("enableVsync", "垂直同步", "画面", "true", Kind::Boolean);
        add("fullscreen", "全屏", "画面", "false", Kind::Boolean);
        add("graphicsMode", "画质", "画面", "1", Kind::Choice, 0, 1, {"0|流畅", "1|高品质", "2|极佳"});
        add("particles", "粒子效果", "画面", "0", Kind::Choice, 0, 1, {"0|全部", "1|减少", "2|最少"});
        add("renderClouds", "云", "画面", "true", Kind::Choice, 0, 1, {"false|关闭", "fast|快速", "true|精细"});
        add("entityShadows", "实体阴影", "画面", "true", Kind::Boolean);
        add("bobView", "视角摇晃", "画面", "true", Kind::Boolean);
        add("autoJump", "自动跳跃", "操作", "false", Kind::Boolean);
        add("mouseSensitivity", "鼠标灵敏度", "操作", "0.5", Kind::Fraction);
        add("invertYMouse", "反转鼠标", "操作", "false", Kind::Boolean);
        add("rawMouseInput", "原始鼠标输入", "操作", "true", Kind::Boolean);
        add("toggleCrouch", "潜行方式", "操作", "false", Kind::Choice, 0, 1, {"false|按住", "true|切换"});
        add("toggleSprint", "疾跑方式", "操作", "false", Kind::Choice, 0, 1, {"false|按住", "true|切换"});
        add("mainHand", "主手", "操作", "right", Kind::Choice, 0, 1, {"right|右手", "left|左手"});
        add("pauseOnLostFocus", "失去焦点时暂停", "操作", "true", Kind::Boolean);
        add("showSubtitles", "字幕", "辅助功能", "false", Kind::Boolean);
        add("highContrast", "高对比度", "辅助功能", "false", Kind::Boolean);
        add("hideLightningFlash", "隐藏闪电闪光", "辅助功能", "false", Kind::Boolean);
        add("screenEffectScale", "屏幕扭曲效果", "辅助功能", "1.0", Kind::Fraction);
        add("fovEffectScale", "视角变化效果", "辅助功能", "1.0", Kind::Fraction);
        add("darknessEffectScale", "黑暗效果", "辅助功能", "1.0", Kind::Fraction);
        add("chatVisibility", "聊天显示", "聊天", "0", Kind::Choice, 0, 1, {"0|显示", "1|仅命令", "2|隐藏"});
        add("chatColors", "聊天颜色", "聊天", "true", Kind::Boolean);
        add("chatLinks", "聊天链接", "聊天", "true", Kind::Boolean);
        add("chatLinksPrompt", "打开链接前确认", "聊天", "true", Kind::Boolean);
        add("chatOpacity", "聊天不透明度", "聊天", "1.0", Kind::Fraction);
        add("chatScale", "聊天缩放", "聊天", "1.0", Kind::Fraction);
        const QStringList sounds{"master|主音量", "music|音乐", "record|唱片", "weather|天气", "block|方块",
                                 "hostile|敌对生物", "neutral|友好生物", "player|玩家", "ambient|环境", "voice|语音"};
        for (const auto &sound : sounds) {
            auto pair = sound.split('|');
            result.append({"soundCategory_" + pair[0], pair[1], "声音", "1.0", Kind::Fraction});
        }
        const QStringList keys{
            "forward|前进|w", "back|后退|s", "left|左移|a", "right|右移|d", "jump|跳跃|space",
            "sneak|潜行|left.shift", "sprint|疾跑|left.control", "inventory|物品栏|e", "drop|丢弃物品|q",
            "swapOffhand|交换副手物品|f", "chat|打开聊天|t", "command|输入命令|slash",
            "playerlist|玩家列表|tab", "togglePerspective|切换视角|f5", "screenshot|截图|f2",
            "fullscreen|切换全屏|f11", "advancements|进度|l"};
        for (const auto &key : keys) {
            auto pair = key.split('|');
            result.append({"key_key." + pair[0], pair[1], "按键", "key.keyboard." + pair[2], Kind::Key});
        }
        result.append({"key_key.attack", "攻击 / 破坏", "按键", "key.mouse.left", Kind::Key});
        result.append({"key_key.use", "使用 / 放置", "按键", "key.mouse.right", Kind::Key});
        result.append({"key_key.pickItem", "选取方块", "按键", "key.mouse.middle", Kind::Key});
        for (int i = 1; i <= 9; ++i)
            result.append({"key_key.hotbar." + QString::number(i), "快捷栏 " + QString::number(i),
                           "按键", "key.keyboard." + QString::number(i), Kind::Key});
        return result;
    }();
    return options;
}
inline const Option *find(const QString &key) {
    for (const auto &option : all()) if (option.key == key) return &option;
    return nullptr;
}
}
