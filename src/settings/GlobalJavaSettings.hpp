#pragma once

#include <QObject>
#include <QString>

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
     * @brief Ensures a valid Java runtime is available.
     * @return True if Java can be used.
     */
    bool ensureJavaAvailable();

    /**
     * @brief Ensures the Java runtime supports a named component.
     * @param component Component name such as "java" or "javac".
     * @return True if the component is available.
     */
    bool ensureJavaForComponent(const QString &component);

    /**
     * @brief Ensures the configured Java matches the required major version.
     * @param majorVersion Required Java major version.
     * @param component Optional runtime component name from version.json
     *        (for example "java-runtime-epsilon"), used to pick the exact
     *        runtime Mojang ships for that game version.
     * @return True if matching Java is available.
     */
    bool ensureJavaForVersion(int majorVersion, const QString &component = QString());

    /**
     * @brief Scans local installations for Java versions.
     * @return Map from major version to executable path.
     */
    QMap<int, QString> scanLocalJavaVersions();

    /**
     * @brief Downloads a Java runtime from Mojang for the required version.
     * @param majorVersion Required Java major version.
     * @return True if the download succeeded.
     */
    bool downloadJavaFromMojang(int majorVersion);

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

    /**
     * @brief Returns extra JVM arguments configured by the user.
     *
     * 这就是 PCL「自定义 Java 启动参数」的**全局**入口（持久化键 Java/ExtraArgs，
     * 默认 -XX:+UseG1GC）。在 LauncherManager::generateJvmArguments() 里它是先入列
     * 的那一份，"按版本"的那份（wtfml.meta 的 jvmArgs）排在它之后，所以同一条选项
     * 写两遍时单个实例胜出。
     *
     * NOTE: 界面入口还没做 —— 计划落在 MainWindow 那个目前还是空的 QMenuBar 上。
     *       现在只有直接改配置文件才用得上它。
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
};
