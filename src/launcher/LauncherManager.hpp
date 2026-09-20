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

class GlobalJavaSettings;

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
     * @param settings Java runtime settings.
     * @param offlineMode Whether to launch in offline mode.
     * @param javaMajorVersion 实际使用的 Java 主版本，用于过滤该版本不支持的 JVM 选项。
     * @return 返回 JVM 参数列表。
     */
    QStringList generateJvmArguments(const QString &token, const QString &versionId,
                                     const QString &assetIndexId, const QJsonObject &versionJson,
                                     const GlobalJavaSettings &settings,
                                     bool offlineMode = false, int javaMajorVersion = 0) const;

    /**
     * @brief 启动 Minecraft 游戏进程。
     * @param arguments JVM and game arguments.
     * @param settings Java runtime settings.
     * @param versionId Version identifier.
     * @return 进程是否真的起来了。三条早退路径（进程已在运行、找不到 Java 可执行
     *         文件、waitForStarted 超时）都返回 false —— 调用方用它决定要不要把
     *         这个版本记成"上次启动的版本"，所以失败时绝不能返回 true。
     *
     * NOTE: 返回 true 只代表"java 进程起来了"，不代表"游戏没崩"。后面游戏因为
     *       库不全之类的原因立刻退出时它仍然是 true，这是刻意的：那种情况恰恰
     *       最需要下次打开还选着这个版本。
     */
    bool startGame(const QStringList &arguments, GlobalJavaSettings &settings,
                   const QString &versionId);

signals:
    void processOutput(const QString &text);
    void processError(const QString &text);
    void gameFinished(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess *m_gameProcess;
};
