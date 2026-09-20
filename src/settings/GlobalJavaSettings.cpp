/**
 * @file GlobalJavaSettings.cpp
 * @brief Manages Java runtime settings and version detection for Minecraft.
 * @author LoserKugua
 * @date 2026-04-10
 * @note Implements Java discovery and version verification logic.
 *
 * Handles Java executable discovery, version parsing, memory settings,
 * and ensures appropriate Java runtime for specific Minecraft versions.
 */
#include "GlobalJavaSettings.hpp"

#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "../util/AppPaths.hpp"

namespace {
static constexpr const char kJavaPath[] = "Java/Path";
static constexpr const char kXmx[] = "Java/Xmx";
static constexpr const char kXms[] = "Java/Xms";
static constexpr const char kExtraArgs[] = "Java/ExtraArgs";

bool runProcess(const QString &program, const QStringList &arguments, QString *stdOut = nullptr,
                QString *stdErr = nullptr, int timeoutMs = 5000) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        return false;
    }

    if (stdOut) {
        *stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    }
    if (stdErr) {
        *stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    }
    return process.exitStatus() == QProcess::NormalExit;
}

bool verifyJavaExecutable(const QString &path, QString *versionOut = nullptr) {
    if (path.isEmpty()) {
        return false;
    }

    QString execPath = path;
    QFileInfo info(execPath);
    if (info.isDir()) {
        execPath = QDir(execPath).absoluteFilePath("bin/java.exe");
    }

    if (!QFileInfo(execPath).exists()) {
        return false;
    }

    QString stderrOutput;
    if (!runProcess(execPath, {"-version"}, nullptr, &stderrOutput, 5000)) {
        return false;
    }

    if (stderrOutput.contains("version")) {
        if (versionOut) {
            *versionOut = stderrOutput;
        }
        return true;
    }
    return false;
}

int parseJavaMajorVersion(const QString &versionOutput) {
    // Java version output examples:
    // java version "1.8.0_301" -> 8
    // java version "11.0.12" -> 11
    // java version "17.0.1" -> 17
    // java version "21.0.1" -> 21

    // Java requirements for games:
    // 1.6 - 1.16.5 -> Java 8
    // 1.17 - 1.20.4 -> Java 17
    // 1.20.5 - 1.21.11 -> Java 21
    // 26.1 + -> Java 25

    QRegularExpression regex("version \"([^\"]+)\"");
    QRegularExpressionMatch match = regex.match(versionOutput);
    if (!match.hasMatch()) {
        return -1;
    }

    QString version = match.captured(1);
    QStringList parts = version.split('.');
    if (parts.isEmpty()) {
        return -1;
    }

    bool ok;
    int majorVersion = parts[0].toInt(&ok);
    if (!ok) {
        return -1;
    }

    // For Java 8 and earlier, version is "1.8.x" so major version is the second part
    if (majorVersion == 1 && parts.size() > 1) {
        majorVersion = parts[1].toInt(&ok);
        if (!ok) {
            return -1;
        }
    }

    return majorVersion;
}

/**
 * @brief 判断某个 Java 可执行文件的主版本是否与需求一致。
 * @param path Java 可执行文件或包含 bin/java.exe 的运行时目录。
 * @param major 需要的 Java 主版本（如 8 / 17 / 21 / 25），传 -1 表示不限版本。
 * @return 若路径有效且版本匹配，则返回 true.
 *
 * NOTE: 环境变量、PATH、常见安装目录里找到的 Java 都必须先过这一关才能采用。
 * 否则会静默选中错误的 Java（例如用 Java 21 去启动需要 Java 25 的 26.x，
 * 最终表现为 UnsupportedClassVersionError 而不是一条能看懂的提示）。
 */
bool javaMatchesMajor(const QString &path, int major) {
    if (path.isEmpty()) {
        return false;
    }
    QString versionOutput;
    if (!verifyJavaExecutable(path, &versionOutput)) {
        return false;
    }
    return major < 0 || parseJavaMajorVersion(versionOutput) == major;
}

/**
 * @brief 在 JAVA_HOME / JDK_HOME 指向的运行时里查找 Java。
 * @param requiredMajor 需要的 Java 主版本，-1 表示不限。
 * @return 若找到匹配的 java.exe 则返回其路径，否则返回空.
 */
QString findJavaInEnvironment(int requiredMajor = -1) {
    const QStringList homeVars = {qEnvironmentVariable("JAVA_HOME"),
                                  qEnvironmentVariable("JDK_HOME")};
    for (const QString &home : homeVars) {
        if (home.isEmpty()) {
            continue;
        }
        const QString candidate = QDir(home).absoluteFilePath("bin/java.exe");
        if (javaMatchesMajor(candidate, requiredMajor)) {
            return candidate;
        }
    }
    return QString();
}

/**
 * @brief 在 Program Files 下递归查找常见发行版的 Java。
 * @param requiredMajor 需要的 Java 主版本，-1 表示不限。
 * @return 若找到匹配的 java.exe 则返回其路径，否则返回空.
 *
 * NOTE: 这里会遍历整个 Program Files，比较慢，因此只在更便宜的候选源
 * （已配置路径、本地运行时、环境变量、PATH）都没命中时才会走到。
 */
QString findJavaInCommonLocations(int requiredMajor = -1) {
    const QStringList rootDirs = {qEnvironmentVariable("ProgramFiles"),
                                  qEnvironmentVariable("ProgramFiles(x86)"),
                                  qEnvironmentVariable("ProgramW6432")};

    for (const QString &rootDirPath : rootDirs) {
        if (rootDirPath.isEmpty()) {
            continue;
        }
        QDir rootDir(rootDirPath);
        if (!rootDir.exists()) {
            continue;
        }

        QDirIterator it(rootDir.absolutePath(), QStringList() << "java.exe", QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString candidate = it.next();
            if (candidate.contains("Java", Qt::CaseInsensitive) ||
                candidate.contains("jdk", Qt::CaseInsensitive) ||
                candidate.contains("jre", Qt::CaseInsensitive) ||
                candidate.contains("temurin", Qt::CaseInsensitive) ||
                candidate.contains("adoptium", Qt::CaseInsensitive) ||
                candidate.contains("corretto", Qt::CaseInsensitive) ||
                candidate.contains("microsoft", Qt::CaseInsensitive) ||
                candidate.contains("zulu", Qt::CaseInsensitive)) {
                if (javaMatchesMajor(candidate, requiredMajor)) {
                    return candidate;
                }
            }
        }
    }

    return QString();
}

/**
 * @brief 查找 PATH 上的 java。
 * @param requiredMajor 需要的 Java 主版本，-1 表示不限。
 * @return 若找到匹配的 java.exe 则返回其绝对路径，否则返回空.
 *
 * FIXME: 旧实现直接返回裸命令 "java"，但 verifyJavaExecutable() 第一步就是
 * QFileInfo::exists()，裸命令永远过不了；即使侥幸通过，startGame() 也会用
 * QFileInfo(program).exists() 判定"Java 可执行文件未找到"。必须解析成绝对路径。
 */
QString findJavaOnPath(int requiredMajor = -1) {
    const QString resolved = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!resolved.isEmpty() && javaMatchesMajor(resolved, requiredMajor)) {
        return resolved;
    }
    return QString();
}

/**
 * @brief 扫描原版启动器已经下载好的运行时目录。
 *
 * 原版启动器会把 Mojang 的运行时解压到
 * %APPDATA%/.minecraft/runtime/<component>/，目录名与 version.json 里的
 * javaVersion.component 一一对应（jre-legacy / java-runtime-alpha / beta /
 * gamma / delta / epsilon）。直接复用这些运行时可以省掉一次完整的下载，
 * 而且它们是 Mojang 针对该游戏版本挑选过的。
 *
 * @return 组件名到 java.exe 绝对路径的映射。
 */
QMap<QString, QString> scanVanillaRuntimeComponents() {
    QMap<QString, QString> components;

    QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty()) {
        appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    }
    if (appData.isEmpty()) {
        return components;
    }

    QDir runtimeRoot(QDir(appData).absoluteFilePath(".minecraft/runtime"));
    if (!runtimeRoot.exists()) {
        return components;
    }

    const QStringList subdirs = runtimeRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subdir : subdirs) {
        const QString exe = QDir(runtimeRoot.absoluteFilePath(subdir)).absoluteFilePath("bin/java.exe");
        if (verifyJavaExecutable(exe)) {
            components[subdir] = exe;
        }
    }
    return components;
}

bool extractZipWithPowerShell(const QString &zipFile, const QString &targetDir) {
    QString command = QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                          .arg(zipFile, targetDir);
    QString stdOut, stdErr;
    bool ok = runProcess(QStringLiteral("powershell"),
                         {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), command},
                         &stdOut, &stdErr, 60000);
    Q_UNUSED(stdOut);
    return ok;
}

QString findExtractedJava(const QString &rootDir) {
    QDirIterator it(rootDir, QStringList() << "java.exe", QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString candidate = it.next();
        if (verifyJavaExecutable(candidate)) {
            return candidate;
        }
    }
    return QString();
}

/**
 * @brief 若目录下只有一个子目录且没有别的文件，则把该子目录的内容上移一层。
 * @param dir 待压平的目录。
 * @return 若发生了移动则返回 true.
 *
 * 所有 Java 运行时压缩包都自带一层以版本号命名的根目录（如 jdk-21.0.4+7-jre/），
 * 压平一层之后布局才会稳定成 <base>/<component>/bin/java.exe，
 * 与 scanLocalJavaVersions() 的目录约定一致。
 */
bool flattenSingleRootDir(const QString &dir) {
    QDir root(dir);
    if (!root.exists()) {
        return false;
    }

    const QStringList entries = root.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    if (entries.size() != 1) {
        return false;
    }

    QDir onlyChild(root.absoluteFilePath(entries.first()));
    if (!onlyChild.exists()) {
        return false;  // 唯一项是个文件，没什么可压平的
    }

    const QStringList children =
        onlyChild.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &child : children) {
        const QString target = root.absoluteFilePath(child);
        if (QFileInfo::exists(target)) {
            continue;
        }
        QDir().rename(onlyChild.absoluteFilePath(child), target);
    }
    onlyChild.removeRecursively();
    return true;
}

bool downloadFileToLocal(const QUrl &url, const QString &destinationPath) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return false;
    }

    QFile file(destinationPath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->deleteLater();
        return false;
    }
    file.write(reply->readAll());
    file.close();
    reply->deleteLater();
    return true;
}

/**
 * @brief 按 version.json 里声明的 component 名下载对应的 Java 运行时。
 * @param destinationDir 运行时根目录（通常是 <.minecraft>/java）。
 * @param component Mojang 的运行时组件名，如 java-runtime-epsilon。
 * @param javaPathOut 解压后找到的 java.exe 路径。
 * @return 若下载并解压成功则返回 true.
 */
bool downloadJavaRuntime(const QString &destinationDir, const QString &component,
                         QString *javaPathOut) {
    QList<QUrl> urls;
    if (component == "java-runtime-epsilon") {
        // Java 25 —— Minecraft 26.1 及以后要求该运行时
        urls = {QUrl("https://download.oracle.com/java/25/latest/jdk-25_windows-x64_bin.zip"),
                QUrl("https://download.bell-sw.com/java/25.0.2+12/"
                     "bellsoft-jdk25.0.2+12-windows-amd64.zip")};
    } else if (component == "java-runtime-gamma" || component == "java-runtime-delta") {
        // Java 21
        urls = {
            QUrl("https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.4+7/"
                 "OpenJDK21U-jre_x64_windows_hotspot_21.0.4_7.zip"),
            QUrl("https://download.bell-sw.com/java/21.0.4+7/bellsoft-jre21.0.4+7-win-amd64.zip")};
    } else if (component == "java-runtime-beta") {
        // Java 17
        urls = {
            QUrl("https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.12+7/"
                 "OpenJDK17U-jre_x64_windows_hotspot_17.0.12_7.zip"),
            QUrl(
                "https://download.bell-sw.com/java/17.0.12+7/bellsoft-jre17.0.12+7-win-amd64.zip")};
    } else if (component == "java-runtime-alpha" || component == "jre-legacy") {
        // Java 8
        urls = {
            QUrl("https://github.com/adoptium/temurin8-binaries/releases/download/jdk8u422-b05/"
                 "OpenJDK8U-jre_x64_windows_hotspot_8u422b05.zip"),
            QUrl("https://download.bell-sw.com/java/8u422+5/bellsoft-jre8u422+5-win-amd64.zip")};
    } else {
        // Default to Java 21
        urls = {
            QUrl("https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.4+7/"
                 "OpenJDK21U-jre_x64_windows_hotspot_21.0.4_7.zip"),
            QUrl("https://download.bell-sw.com/java/21.0.4+7/bellsoft-jre21.0.4+7-win-amd64.zip")};
    }

    // 以 component 名为子文件夹，下载和解压内容都放进去
    const QString subDir = QDir(destinationDir).absoluteFilePath(component);
    QDir().mkpath(subDir);
    const QString tempZip = QDir(subDir).absoluteFilePath("java_runtime.zip");

    for (const QUrl &url : urls) {
        if (!downloadFileToLocal(url, tempZip)) {
            continue;
        }

        // 解压到子文件夹
        if (!extractZipWithPowerShell(tempZip, subDir)) {
            continue;
        }
        QFile::remove(tempZip);

        // 压平压缩包自带的那层版本号根目录，得到 <component>/bin/java.exe
        flattenSingleRootDir(subDir);

        // NOTE: 必须在刚解压的子目录里找。搜索父目录会命中别的 component
        // 之前解压好的 java.exe，导致版本看起来"匹配"其实不是这一份。
        const QString javaPath = findExtractedJava(subDir);
        if (!javaPath.isEmpty()) {
            if (javaPathOut) {
                *javaPathOut = javaPath;
            }
            return true;
        }
    }

    return false;
}
}  // namespace

GlobalJavaSettings::GlobalJavaSettings(QObject *parent)
    : QObject(parent),
      m_xmx(4096)  // 4GB
      ,
      m_xms(1024)  // 1GB
      ,
      m_extraJvmArgs(QStringLiteral("-XX:+UseG1GC")) {
    load();
}

QString GlobalJavaSettings::javaPath() const { return m_javaPath; }

void GlobalJavaSettings::setJavaPath(const QString &path) { m_javaPath = path; }

int GlobalJavaSettings::xmx() const { return m_xmx; }

void GlobalJavaSettings::setXmx(int mbytes) { m_xmx = qMax(256, mbytes); }

int GlobalJavaSettings::xms() const { return m_xms; }

void GlobalJavaSettings::setXms(int mbytes) { m_xms = qMax(128, mbytes); }

QString GlobalJavaSettings::extraJvmArgs() const { return m_extraJvmArgs; }

void GlobalJavaSettings::setExtraJvmArgs(const QString &args) { m_extraJvmArgs = args; }

void GlobalJavaSettings::load() {
    QSettings settings;
    m_javaPath = settings.value(kJavaPath).toString();
    m_xmx = settings.value(kXmx, m_xmx).toInt();
    m_xms = settings.value(kXms, m_xms).toInt();
    m_extraJvmArgs = settings.value(kExtraArgs, m_extraJvmArgs).toString();
}

void GlobalJavaSettings::save() const {
    QSettings settings;
    settings.setValue(kJavaPath, m_javaPath);
    settings.setValue(kXmx, m_xmx);
    settings.setValue(kXms, m_xms);
    settings.setValue(kExtraArgs, m_extraJvmArgs);
}

QMap<int, QString> GlobalJavaSettings::scanLocalJavaVersions() {
    QMap<int, QString> versions;
    const QString javaDir = AppPaths::javaDir();

    QDir dir(javaDir);
    if (dir.exists()) {
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &subdir : subdirs) {
            const QString javaExe = dir.absoluteFilePath(subdir + "/bin/java.exe");
            QString versionOutput;
            if (!verifyJavaExecutable(javaExe, &versionOutput)) {
                continue;
            }
            const int majorVersion = parseJavaMajorVersion(versionOutput);
            if (majorVersion > 0) {
                versions[majorVersion] = javaExe;
            }
        }
    }

    // 原版启动器已经下载好的运行时（jre-legacy / java-runtime-*）同样可以复用，
    // 省掉一次完整下载。这里用 first-wins，优先保留启动器自己下载的那一份。
    const QMap<QString, QString> vanillaComponents = scanVanillaRuntimeComponents();
    for (auto it = vanillaComponents.cbegin(); it != vanillaComponents.cend(); ++it) {
        QString versionOutput;
        if (!verifyJavaExecutable(it.value(), &versionOutput)) {
            continue;
        }
        const int majorVersion = parseJavaMajorVersion(versionOutput);
        if (majorVersion > 0 && !versions.contains(majorVersion)) {
            qDebug() << "Reusing vanilla launcher runtime" << it.key() << "for Java" << majorVersion;
            versions[majorVersion] = it.value();
        }
    }

    return versions;
}

bool GlobalJavaSettings::downloadJavaFromMojang(int majorVersion) {
    qDebug() << "Starting Java download from Adoptium for version:" << majorVersion;

    // Use Adoptium API to download Java
    const QString apiUrl =
        QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot").arg(majorVersion);
    qDebug() << "Fetching from Adoptium API:" << apiUrl;

    QNetworkAccessManager manager;
    QNetworkRequest request(apiUrl);
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "Network error fetching from Adoptium API:" << reply->errorString();
        reply->deleteLater();
        return false;
    }

    qDebug() << "Adoptium API response received successfully";
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    QJsonArray assets = doc.array();
    if (assets.isEmpty()) {
        qDebug() << "No assets found in Adoptium API response";
        return false;
    }

    // Find Windows x64 JRE
    QString downloadUrl;
    QString jreUrl;
    QString jdkUrl;
    for (const QJsonValue &asset : assets) {
        QJsonObject assetObj = asset.toObject();
        QJsonObject binary = assetObj["binary"].toObject();

        QString os = binary["os"].toString();
        QString arch = binary["architecture"].toString();
        QString imageType = binary["image_type"].toString();

        if (os == "windows" && arch == "x64") {
            QJsonObject package = binary["package"].toObject();
            const QString link = package["link"].toString();
            if (link.isEmpty()) {
                continue;
            }
            // Adoptium 对较新的 Java（25+）未必发布 JRE 构建，所以优先取 JRE，
            // 没有 JRE 时退回 JDK —— 两者都能跑游戏，JDK 只是体积大一些。
            if (imageType == "jre" && jreUrl.isEmpty()) {
                jreUrl = link;
            } else if (imageType == "jdk" && jdkUrl.isEmpty()) {
                jdkUrl = link;
            }
        }
    }

    downloadUrl = jreUrl.isEmpty() ? jdkUrl : jreUrl;
    if (downloadUrl.isEmpty()) {
        qDebug() << "No suitable Windows x64 JRE/JDK found in Adoptium API response";
        return false;
    }
    qDebug() << "Chosen download URL:" << downloadUrl;

    // Download the zip
    const QString downloadDir = AppPaths::javaVersionDir(majorVersion);
    qDebug() << "Download directory:" << downloadDir;
    QDir().mkpath(downloadDir);
    QString tempZip = QDir(downloadDir).absoluteFilePath("java_runtime.zip");
    qDebug() << "Temp ZIP path:" << tempZip;

    qDebug() << "Downloading ZIP from:" << downloadUrl;
    if (!downloadFileToLocal(QUrl(downloadUrl), tempZip)) {
        qDebug() << "Failed to download ZIP file";
        return false;
    }
    qDebug() << "ZIP downloaded successfully";

    qDebug() << "Extracting ZIP...";
    if (!extractZipWithPowerShell(tempZip, downloadDir)) {
        qDebug() << "Failed to extract ZIP";
        QFile::remove(tempZip);
        return false;
    }
    qDebug() << "ZIP extracted successfully";
    QFile::remove(tempZip);

    // 压平压缩包自带的版本号根目录，使布局稳定为 java/<majorVersion>/bin/java.exe，
    // 这样 scanLocalJavaVersions() 下次能直接认得出来。
    flattenSingleRootDir(downloadDir);

    qDebug() << "Finding extracted Java...";
    QString javaPath = findExtractedJava(downloadDir);
    if (!javaPath.isEmpty()) {
        qDebug() << "Found Java at:" << javaPath;
        m_javaPath = javaPath;
        save();
        return true;
    }

    qDebug() << "No Java executable found after extraction";
    return false;
}
/**
 * @brief Ensures the correct Java version is available for the specified Minecraft version.
 *
 * 候选顺序：已配置路径 → 本地已安装运行时（含原版启动器下载的 java-runtime-*）
 * → JAVA_HOME/JDK_HOME → PATH → 常见安装目录 → 按 component 下载 → 按主版本下载。
 * 前五个来源找到的 Java **必须主版本匹配**才采用，否则会静默选中错误的 Java
 * （例如拿 Java 21 去启动需要 Java 25 的 26.x，只会得到 UnsupportedClassVersionError）。
 *
 * @param majorVersion Required Java major version (e.g., 8, 11, 17, 21, 25)
 * @param component Optional runtime component name from version.json,
 *        e.g. "java-runtime-epsilon"
 * @return true if appropriate Java is available and configured, false otherwise
 */
bool GlobalJavaSettings::ensureJavaForVersion(int majorVersion, const QString &component) {
    qDebug() << "Ensuring Java for version:" << majorVersion << "component:" << component;

    // First, check if we already have the correct version configured
    if (!m_javaPath.isEmpty()) {
        if (javaMatchesMajor(m_javaPath, majorVersion)) {
            qDebug() << "Using configured Java version" << majorVersion << "at:" << m_javaPath;
            return true;
        }
        qDebug() << "Configured Java does not match required version" << majorVersion << ":"
                 << m_javaPath;
    }

    // Scan local Java versions
    QMap<int, QString> localVersions = scanLocalJavaVersions();
    if (localVersions.contains(majorVersion)) {
        QString path = localVersions[majorVersion];
        qDebug() << "Found matching local Java version" << majorVersion << "at:" << path;
        m_javaPath = path;
        save();
        return true;
    }

    // Try to find in environment
    QString candidate = findJavaInEnvironment(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java in environment:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaOnPath(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java on PATH:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaInCommonLocations(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java in common locations:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    // 按 version.json 声明的 component 下载对应运行时
    if (!component.isEmpty()) {
        qDebug() << "Downloading runtime component" << component << "for Java" << majorVersion;
        const QString javaBase = AppPaths::javaDir();
        QString downloaded;
        if (downloadJavaRuntime(javaBase, component, &downloaded) &&
            javaMatchesMajor(downloaded, majorVersion)) {
            qDebug() << "Downloaded and verified Java" << majorVersion << "at:" << downloaded;
            m_javaPath = downloaded;
            save();
            return true;
        }
        qDebug() << "Component" << component << "did not provide a matching Java, falling back";
    }

    // Download from Adoptium as a last resort
    qDebug() << "No matching Java found, downloading version" << majorVersion << "from Adoptium...";
    if (downloadJavaFromMojang(majorVersion)) {
        qDebug() << "Downloaded Java version" << majorVersion;
        return true;
    }

    qDebug() << "Failed to obtain Java version" << majorVersion;
    return false;
}
