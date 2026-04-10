/**
 * @file LauncherManager.hpp
 * @brief Declares JVM argument generation and game process management.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 负责 Minecraft 启动参数构建和进程生命周期管理。
 */
#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class GlobalJavaSettings;
class QJsonObject;

/**
 * @brief Handles Minecraft JVM argument generation and process startup.
 *
 * Builds the classpath, applies platform-specific rules, and starts
 * the Minecraft process with the configured Java runtime.
 */
class LauncherManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a LauncherManager instance.
     * @param parent Parent QObject.
     */
    explicit LauncherManager(QObject *parent = nullptr);

    /**
     * @brief Validates required game files before launch.
     * @param versionId Minecraft version identifier.
     * @param errors Optional output list for error descriptions.
     * @return True if pre-launch checks pass.
     */
    bool checkPreLaunch(const QString &versionId, QStringList *errors = nullptr) const;

    /**
     * @brief Generates JVM arguments for Minecraft launch.
     * @param token Authentication token.
     * @param versionId Version identifier.
     * @param assetIndexId Asset index identifier.
     * @param versionJson Version JSON manifest.
     * @param settings Java runtime settings.
     * @param offlineMode Whether to launch in offline mode.
     * @return List of JVM arguments.
     */
    QStringList generateJvmArguments(const QString &token, const QString &versionId,
                                     const QString &assetIndexId, const QJsonObject &versionJson,
                                     const GlobalJavaSettings &settings,
                                     bool offlineMode = false) const;

    /**
     * @brief Starts the Minecraft game process.
     * @param arguments JVM and game arguments.
     * @param settings Java runtime settings.
     * @param versionId Version identifier.
     */
    void startGame(const QStringList &arguments, GlobalJavaSettings &settings,
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
