/**
 * @file LauncherManager.hpp
 * @brief 为 JVM 参数生成和游戏进程管理进行声明。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 负责 Minecraft 启动参数构建和进程生命周期管理。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include "../settings/EffectiveJavaSettings.hpp"

/**
 * @brief 处理 Minecraft 的 JVM 参数生成和启动过程。
 *
 * 构建 classpath, 应用平台特定规定，使用配置的 Java运行时环境 启动 Minecraft 进程。
 */
class LauncherManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 建立一个启动管理实例。
     * @param parent Parent QObject.
     */
    explicit LauncherManager(QObject *parent = nullptr);

    /**
     * @brief 在启动前验证游戏文件。
     * @param versionId Minecraft version identifier.
     * @param errors Optional output list for error descriptions.
     * @param versionJson Version manifest, used to check the natives requirement.
     * @return 若通过启动前检查通过，则返回 true.
     */
    bool checkPreLaunch(const QString &versionId, QStringList *errors = nullptr,
                        const QJsonObject &versionJson = QJsonObject()) const;

    /**
     * @brief 按需把旧格式的 natives classifier 解压到该版本自己的目录。
     * @param versionJson Version manifest containing library dependencies.
     * @param versionId Minecraft version identifier.
     * @param errors Optional output list for error descriptions.
     * @return 返回 natives 目录的绝对路径；该版本不需要 natives，或准备失败时返回空。
     *
     * NOTE: 解压只在该目录还没有任何 .dll 时才做，所以除第一次启动外几乎零开销。
     */
    QString prepareNatives(const QJsonObject &versionJson, const QString &versionId,
                           QStringList *errors = nullptr) const;

    /**
     * @brief 为 Minecraft 启动生成 JVM 参数。
     * @param token Authentication token.
     * @param versionId Version identifier.
     * @param assetIndexId Asset index identifier.
     * @param versionJson Version JSON manifest.
     * @param settings 这次启动**真的要用的**那一套 Java 值（全局按实例覆盖合成之后
     *        的结果，见 resolveJavaSettings()）。收 EffectiveJavaSettings 而不是
     *        GlobalJavaSettings 是刻意的：启动器只该知道"这次用什么值"，
     *        不该知道它是全局档还是某个实例的覆盖 —— 那样每加一层作用域都要改这里。
     * @param offlineMode Whether to launch in offline mode.
     * @param javaMajorVersion 实际使用的 Java 主版本，用于过滤该版本不支持的 JVM 选项。
     * @param versionJvmArgs 该版本自定义的 JVM 参数（wtfml.meta 的 "jvmArgs"），
     *        来自 VersionManager::versionJvmArgs()。插在全局
     *        GlobalJavaSettings::extraJvmArgs() **之后** —— JVM 参数后写的胜出，
     *        所以单个实例能盖掉全局默认那套（例如全局写死了 -XX:+UseG1GC）。
     * @param playerName 玩家名，写进 --username。离线与正版两条分支**都用这一个来源**
     *        （调用方传 AuthManager::playerName()）—— 以前两条分支各写各的：正版写死
     *        "Player"，离线是 "Player" 拼一个时间戳。空 = 调用方没给（测试、未登录），
     *        退回默认名。**不能发一个空的 --username 给游戏**，那样游戏会拒绝启动，
     *        所以空值是"退回默认"而不是"原样传下去"。
     * @param playerUuid 正版分支的 --uuid（调用方传 AuthManager::playerUuid()，带横线的
     *        标准写法）。空 = 没有档案（没登录、或者测试没给），那时退回全零 UUID ——
     *        这是老行为：以前登录流程根本不拉档案，全工程没有 UUID 来源，所以正版分支一
     *        直发全零，正版服上等于一个"谁都不是"的身份。离线分支**不用**它（那边每次
     *        自己随机生成一个）。
     * @return 返回 JVM 参数列表。
     *
     * NOTE: 调用方传 javaMajorVersion <= 0（未传）时按"**沿用旧行为**"处理，不是"一条都
     *       不加"：17 门槛的 --enable-native-access 在 <=0 时**照样加**（cpp 里的判断是
     *       `>= 17 || <= 0`，旁边注明 0 表示调用方没传）；不加的只有 19 门槛的
     *       -Dstdout/stderr.encoding 和 25 门槛的 --sun-misc-unsafe-memory-access。
     */
    QStringList generateJvmArguments(const QString &token, const QString &versionId,
                                     const QString &assetIndexId, const QJsonObject &versionJson,
                                     const EffectiveJavaSettings &settings,
                                     bool offlineMode = false, int javaMajorVersion = 0,
                                     const QStringList &versionJvmArgs = QStringList(),
                                     const QString &playerName = QString(),
                                     const QString &playerUuid = QString()) const;

    /**
     * @brief 启动 Minecraft 游戏进程。
     * @param arguments JVM and game arguments.
     * @param settings 这次启动要用的 Java 值（只读它里面的 javaPath；改成 const 引用
     *        是安全的，也是上一版就有的行为 —— 这里从来不改设置）。
     * @param versionId Version identifier.
     * @return 进程是否真的起来了。三条早退路径（进程已在运行、找不到 Java 可执行
     *         文件、waitForStarted 超时）都返回 false —— 调用方用它决定要不要把
     *         这个版本记成"上次启动的版本"，所以失败时绝不能返回 true。
     *
     * NOTE: 返回 true 只代表"java 进程起来了"，不代表"游戏没崩"。后面游戏因为
     *       库不全之类的原因立刻退出时它仍然是 true，这是刻意的：那种情况恰恰
     *       最需要下次打开还选着这个版本。
     */
    bool startGame(const QStringList &arguments, const EffectiveJavaSettings &settings,
                   const QString &versionId);

    /**
     * @brief 当前正在运行的游戏进程是哪个版本。
     * @return 返回版本号；没有任何游戏在运行时返回空串。
     *
     * 底栏右下角那条"实例运行中"就靠它：它为空 = 没在跑，非空 = 跑的就是这个版本。
     * 记的时机和 m_gameProcess 的状态是同一个（waitForStarted 成功那一刻），清的时机
     * 也同一个（handleProcessFinished），所以不会出现"进程没了但这里还留着版本号"。
     */
    QString runningVersionId() const;

    /**
     * @brief 从最终命令行判断游戏进程的输出是不是 UTF-8。
     * @param arguments 交给 QProcess 的完整参数列表。
     * @return 命令行里 -Dstdout.encoding 的值是 utf-8（不区分大小写）时返回 true。
     *
     * 这是解码侧（decodeGameOutput）的**唯一**判据，而且是从实际命令行反推的 ——
     * 不另立一个"我们加没加编码参数"的开关，两处各记一份状态迟早会失配（加参数的
     * 分支改了、解码的分支忘了改，就是一个只在中文 Windows 上出现的乱码 bug）。
     * 做成静态纯函数是为了让验证工程能直接断言这条不变量的两端。
     */
    static bool commandDeclaresUtf8Output(const QStringList &arguments);

signals:
    void processOutput(const QString &text);
    void processError(const QString &text);
    void gameFinished(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    /// 按游戏进程**实际使用的**输出编码解码它的 stdout/stderr。
    /// 判断依据由 startGame() 从命令行上反推出来，见那里的说明。
    QString decodeGameOutput(const QByteArray &bytes) const;

    QProcess *m_gameProcess;
    /// 正在运行的游戏进程对应的版本号。startGame() 在 waitForStarted() 成功之后
    /// 才记下它，handleProcessFinished() 里清掉 —— 三条早退路径都不记。
    QString m_runningVersionId;
    /// 游戏进程的输出是不是 UTF-8（= 命令行里有 -Dstdout.encoding=utf-8）。
    /// 由 startGame() 在启动前设置；四个解码点都按它选解码方式。
    /// 默认 false = 本地代码页，也就是加这个开关之前的行为。
    bool m_gameOutputIsUtf8 = false;
};
