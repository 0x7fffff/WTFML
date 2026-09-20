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
};
