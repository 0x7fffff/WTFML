#pragma once

#include <QObject>
#include <QString>

#include "../util/SystemMemory.hpp"
#include "EffectiveJavaSettings.hpp"

/**
 * @file GlobalJavaSettings.hpp
 * @brief Declares Java runtime configuration and version management.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Provides Java detection and version-specific runtime selection.
 *
 * Responsible for locating or downloading the correct Java runtime
 * required by the selected Minecraft version.
 */
class GlobalJavaSettings : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the Java settings manager.
     * @param parent Parent QObject.
     */
    explicit GlobalJavaSettings(QObject *parent = nullptr);

    /**
     * @brief Returns the configured Java executable path.
     */
    QString javaPath() const;

    /**
     * @brief Sets the Java executable path.
     * @param path Absolute path to the java executable.
     */
    void setJavaPath(const QString &path);

    // ------------------------------------------------------------------
    // 「Java 版本」这一项（设置界面）：自动挑 / 手动钉死
    // ------------------------------------------------------------------
    // 两个档位**不是**"路径空不空"能表达的：自动档也会把挑中的路径写进 m_javaPath
    // （它同时是缓存），所以"路径非空"既可能是自动挑的、也可能是用户钉的，
    // 必须另存一个开关才分得清。

    /**
     * @brief 是否让启动器自己按版本要求挑 Java。
     * @return true = 自动（默认，也是加这一项之前的行为）；
     *         false = 手动指定 —— 路径不匹配时**不换一个**，直接失败。
     */
    bool autoSelectJava() const { return m_autoSelectJava; }

    /**
     * @brief 设置自动/手动档。和其他 setter 一样**只改内存**，落盘调 save()。
     * @param autoSelect true = 自动。
     */
    void setAutoSelectJava(bool autoSelect) { m_autoSelectJava = autoSelect; }

    /**
     * @brief 把"自动挑中的 Java 路径"存回**全局**这一层：改内存 + 落盘。
     * @param path 挑中的路径。和当前值一样时**不写盘** —— 每次启动都重写一遍注册表
     *        没必要，而且那条路径本来就在盘上。
     *
     * 只有全局档该调它。实例档挑中的路径要写回那个实例的 wtfml.meta（见
     * MainWindow::onStartButtonClicked()），写到这里就是跨作用域污染。
     *
     * NOTE: 老代码里这段逻辑长在 ensureJavaForVersion(int, QString) 里面（那个签名
     *       只能改自己，所以它别无选择）。拆出来是为了让值版和它共用同一份
     *       "全局档怎么存"的定义，而不是在两处各写一遍 setJavaPath + save。
     */
    void adoptJavaPath(const QString &path);

    /**
     * @brief 把这一层的值倒成一份**有效值**，供实例覆盖层当底用。
     *
     * 这是"每实例设置"那一层唯一需要全局档提供的东西：合成
     * （resolveJavaSettings()）和自动挑 Java 都只认 EffectiveJavaSettings。
     */
    EffectiveJavaSettings effective() const;

    /**
     * @brief 隐式转换到有效值。等价于 effective()。
     *
     * 选隐式转换而不是让调用方每处都写 effective()，是为了让
     * LauncherManager::generateJvmArguments() / startGame() 换成收
     * `const EffectiveJavaSettings &` 之后，**所有现存调用点一个字都不用改**
     * （验证工程里有二十多处）。顺带 LauncherManager 从此完全不认识这个类，
     * 依赖方向从"启动器知道全局设置"变成"启动器只知道这次要用的值"。
     */
    operator EffectiveJavaSettings() const { return effective(); }

    /**
     * @brief Ensures the configured Java matches the required major version.
     * @param majorVersion Required Java major version.
     * @param component Optional runtime component name from version.json
     *        (for example "java-runtime-epsilon"), used to pick the exact
     *        runtime Mojang ships for that game version.
     * @return True if matching Java is available.
     *
     * 这是**全局档**的老签名：挑中的路径写回本对象并落盘（= 注册表）。
     * 加了实例覆盖之后它变成 ensureJavaForVersion(EffectiveJavaSettings&, ...)
     * 的一层**转发壳** —— 保留它是为了让 E 段那几条断言、以及所有现存调用点不用改，
     * 而且全局档的行为逐字节不变。
     */
    bool ensureJavaForVersion(int majorVersion, const QString &component = QString());

    /**
     * @brief 值版：在一份**给定的**有效值上挑 Java，挑中的路径写回 io。
     *
     * @param io 进参是这次要用的值，出参是挑完之后的值（javaPath 可能被换掉）。
     * @param majorVersion 需要的主版本。
     * @param component version.json 里声明的运行时组件名（可为空）。
     * @param allowAutoSelect 允许自动挑吗。false = **手动档**：io.javaPath 不匹配时
     *        不替用户换一个，直接失败返回 false。
     * @param errorOut 失败原因（给用户看的一句话）。
     * @return 拿到匹配的 Java 返回 true。
     *
     * NOTE: 这个函数**不碰** m_javaPath、**不落盘**、**不改任何成员** —— 挑中的路径
     *       落在哪个作用域由调用方决定：全局档写回注册表（老行为），实例档写回那个
     *       实例的 wtfml.meta。这正是"跨作用域污染"在结构上不可能发生的原因：
     *       实例 A 自动挑中的 Java 绝不会写进全局配置，也就不会覆盖用户手填的全局路径。
     *
     * NOTE: 实例覆盖了 javaPath 就等于**手动档**，调用方要把 allowAutoSelect 传 false。
     *       不这么做的话，用户给某个实例钉死的 Java 会被"按版本要求自动挑"覆盖掉，
     *       那一项覆盖也就白设了。
     */
    bool ensureJavaForVersion(EffectiveJavaSettings &io, int majorVersion,
                              const QString &component, bool allowAutoSelect,
                              QString *errorOut = nullptr);

    /**
     * @brief Scans local installations for Java versions.
     * @return Map from major version to executable path.
     *
     * NOTE: 这是**同步**的，而且每个候选运行时都要认一次主版本（见
     *       majorVersionFromReleaseFile()）。界面在切到设置页那一下调它，
     *       所以它花多少毫秒，界面就冻多少毫秒 —— 认版本那条路快不快要紧。
     */
    QMap<int, QString> scanLocalJavaVersions();

    /**
     * @brief 从 JDK/JRE 根目录那个 release 文件的内容里读出主版本。
     * @param releaseText release 文件的全文。
     * @return 主版本号；文件里没有 JAVA_VERSION、或者版本认不出来时返回 -1。
     *
     * 做成公开的纯函数是为了让验证工程能直接钉这张真值表（理由和 suggestXmxMb 一样）：
     * 它是"扫描本机 Java 时不必启动 JVM"这条快路的前提，解析错了的后果是**静默认不出**
     * 本机已经装好的 Java，然后再去下一个几百 MB 的运行时。
     *
     * 格式：release 文件是 KEY="value" 一行一条。Java 9+ 写 JAVA_VERSION="21.0.7"，
     * Java 8 那代写 JAVA_VERSION="1.8.0_422"（主版本在第二段）。
     */
    static int majorVersionFromReleaseFile(const QString &releaseText);

    /**
     * @brief Downloads a Java runtime from Mojang for the required version.
     * @param majorVersion Required Java major version.
     * @param javaPathOut 非空时写入下载并解压出来的 java.exe 路径。
     * @return True if the download succeeded.
     *
     * NOTE: 名字里的 "FromMojang" 是历史遗留，**实际来源是 Adoptium**（api.adoptium.net
     *       的 v3 assets/latest API），不是 Mojang 的服务器。函数名先不改 —— 改名要动
     *       调用点，收益只是好看。
     * NOTE: 它**不**写 m_javaPath、**不**落盘 —— 这条是加实例覆盖时改的。以前它自己
     *       把路径存进全局配置，于是"给某个实例下载一个运行时"会顺手改掉全局的 Java
     *       路径；现在下载归下载、存哪由调用方决定（见 ensureJavaForVersion 的值版）。
     */
    bool downloadJavaFromMojang(int majorVersion, QString *javaPathOut = nullptr);

    /**
     * @brief Current maximum heap size in megabytes.
     */
    int xmx() const;

    /**
     * @brief Sets the maximum heap size in megabytes.
     * @param mbytes Heap size in megabytes.
     */
    void setXmx(int mbytes);

    /**
     * @brief Current minimum heap size in megabytes.
     */
    int xms() const;

    /**
     * @brief Sets the minimum heap size in megabytes.
     * @param mbytes Heap size in megabytes.
     */
    void setXms(int mbytes);

    // ------------------------------------------------------------------
    // 「内存分配」自动档：按系统内存算 Xmx/Xms
    // ------------------------------------------------------------------
    // NOTE: 「自动」是**每次要用的时候现算**，不是启动时算一次就存下来 —— 同一台机器
    //       开着浏览器和什么都没开，可用内存能差好几 GB，而 JVM 只认启动那一刻的机器状态。
    //       所以 LauncherManager 生成命令行之前会再算一次（见 MainWindow 里那处调用）。

    /// 自动档是不是开着。默认 true；老配置（没有这个键）按"用户有没有自己调过"判断，
    /// 见 load()。
    bool autoMemory() const { return m_memoryAuto; }

    /// 设置自动档并立刻落盘。**不动** m_xmx/m_xms —— 值由 refreshSuggestedMemory() 管。
    void setAutoMemory(bool autoMemory);

    /**
     * @brief 自动档下按当前系统内存重算一次 Xmx/Xms。
     *
     * 手动档下**什么都不做**（一个数字都不许碰），但仍然返回这次的系统读数 ——
     * 界面要拿它写"这台机器现在有多少内存"那句说明。
     *
     * @return 这次的系统内存读数（读不到时两个字段都是 0）。
     */
    SystemMemory::Usage refreshSuggestedMemory();

    /**
     * @brief 建议的最大内存（MB）。
     * @param totalMb 系统物理内存总量；<= 0 表示读不到。
     * @param availableMb 当前可用内存；<= 0 表示读不到。
     *
     * 规则（纯函数，验证工程直接钉这张表，不用去问真的系统），按这个先后顺序：
     *   1. total/4 —— "这台机器该分多少给游戏"的经验值
     *   2. available/2 —— "现在还剩多少能拿，留一半给别的程序"，和上面取小的
     *   3. 夹进 [1024, 8192]
     *   4. 再套物理内存一半的硬顶（小机器上这一步会压过下限，见下）
     *   5. 向下取整到 256 的整数倍，但不小于 256（JVM 侧 setXmx 的下限）
     *
     * 为什么是这几个数：
     *   - 上限 8192：再往上是要跑几百个模组的人才会用的量，留给手动档自己填。
     *   - 下限 1024：再小的 Xmx 连原版 1.20 都紧张。
     *   - 4 排在 3 之后是有意的：2 GB 的机器给 1024 已经是一半内存了，1 GB 的机器只该给
     *     512 —— 在一条本来就紧张的机器上按"最少 1 GB"硬凑，只会让 JVM 连堆都开不起来。
     *   - 256 的整数倍：和界面那两个框的步长一致，免得框里出现 3071 这种数。
     */
    static int suggestXmxMb(qint64 totalMb, qint64 availableMb);

    /**
     * @brief 建议的最小内存（MB）：Xmx 的四分之一，夹在 [512, Xmx]。
     * @param xmxMb 已经定下来的最大内存。
     *
     * Xms > Xmx 会让 JVM 拒绝启动（Initial heap size ... larger than the maximum
     * heap size），所以 Xmx 比 512 还小时下限跟着退到 Xmx 上（setXmx 的下限是 256，
     * 256 那档就返回 256）—— 返回值**永远不会**大于 xmxMb。
     */
    static int suggestXmsMb(int xmxMb);

    /**
     * @brief Returns extra JVM arguments configured by the user.
     *
     * 这就是 PCL「自定义 Java 启动参数」的**全局**入口（持久化键 Java/ExtraArgs，
     * 默认 -XX:+UseG1GC）。在 LauncherManager::generateJvmArguments() 里它是先入列
     * 的那一份，"按版本"的那份（wtfml.meta 的 jvmArgs）排在它之后，所以同一条选项
     * 写两遍时单个实例胜出。
     *
     * NOTE: 界面入口在设置页的「JVM 参数」那一行（ui/MainWindowUI.cpp 的 extraJvmArgsEdit
     *       ＋旁边那个「应用」按钮），经 MainWindow::onApplyExtraJvmArgsClicked() 落盘。
     *       它**不跟 textChanged 存**，必须点「应用」才进配置 —— 免得半条参数进了命令行。
     */
    QString extraJvmArgs() const;

    /**
     * @brief Sets extra JVM arguments.
     * @param args Additional arguments string.
     */
    void setExtraJvmArgs(const QString &args);

    /**
     * @brief Loads persisted Java settings.
     */
    void load();

    /**
     * @brief Saves current Java settings persistently.
     */
    void save() const;

private:
    QString m_javaPath;
    int m_xmx;
    int m_xms;
    QString m_extraJvmArgs;
    /// 「Java 版本」选的是不是自动档。默认 true —— 这就是加这一项之前的唯一行为，
    /// 所以老配置文件（没有这个键）读回来仍然是老行为。
    bool m_autoSelectJava = true;
    /// 「内存分配」选的是不是自动档（按系统内存算）。默认 true 的理由同上；
    /// 老配置的例外处理见 load()。
    bool m_memoryAuto = true;
};
