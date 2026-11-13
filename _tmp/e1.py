import sys; sys.path.insert(0, '_tmp')
import ed

h = 'src/settings/LauncherSettings.hpp'
ed.sub(h, '''    /// 设置「显示游戏日志」并立刻落盘。
    void setShowGameLog(bool show);
''', '''    /// 设置「显示游戏日志」并立刻落盘。
    void setShowGameLog(bool show);

    /// 主页「每日一言」那一行：默认开。关掉 = 不发任何请求、主页不显示也不占位。
    /// 说明文字在设置页「个性化 → 外观」那一组里（见 MainWindowSettingsPage）。
    bool dailyQuote() const { return m_dailyQuote; }

    /// 设置「每日一言」并立刻落盘。
    void setDailyQuote(bool enabled);
''')

ed.sub(h, '''    bool m_showGameLog = false;''', '''    bool m_showGameLog = false;
    bool m_dailyQuote = true;''')

c = 'src/settings/LauncherSettings.cpp'
ed.sub(c, '''static constexpr const char kShowGameLog[] = "Launcher/ShowGameLog";''',
       '''static constexpr const char kShowGameLog[] = "Launcher/ShowGameLog";
// 主页「每日一言」的开关（默认开）。和 ShowGameLog 同一个前缀：都是「启动器自己的一次
// 动作要不要做」，不是某个游戏实例的属性。
static constexpr const char kDailyQuote[] = "Launcher/DailyQuote";''')

ed.sub(c, '''void LauncherSettings::setGameWindowSize(int width, int height) {''',
       '''void LauncherSettings::setDailyQuote(bool enabled) {
    if (m_dailyQuote == enabled) {
        return;
    }
    m_dailyQuote = enabled;
    save();
}

void LauncherSettings::setGameWindowSize(int width, int height) {''')

ed.sub(c, '''    m_showGameLog = settings.value(kShowGameLog, m_showGameLog).toBool();''',
       '''    m_showGameLog = settings.value(kShowGameLog, m_showGameLog).toBool();
    // 默认值取当前成员（构造时是 true）：盘上认不出来的值给 false = 关掉那一行，
    // 方向安全 —— 关着就没有任何外发请求。
    m_dailyQuote = settings.value(kDailyQuote, m_dailyQuote).toBool();''')

ed.sub(c, '''    values.insert(QString::fromLatin1(kShowGameLog), QVariant(m_showGameLog));''',
       '''    values.insert(QString::fromLatin1(kShowGameLog), QVariant(m_showGameLog));
    values.insert(QString::fromLatin1(kDailyQuote), QVariant(m_dailyQuote));''')
