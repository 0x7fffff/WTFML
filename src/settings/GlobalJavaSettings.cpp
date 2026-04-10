/**
 * @file GlobalJavaSettings.cpp
 * @brief Manages Java runtime settings and version detection for Minecraft.
 *
 * Handles Java executable discovery, version parsing, memory settings,
 * and ensures appropriate Java runtime for specific Minecraft versions.
 */

#include "GlobalJavaSettings.hpp"

#include <QCoreApplication>
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

QString findJavaInEnvironment() {
    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty()) {
        const QString candidate = QDir(javaHome).absoluteFilePath("bin/java.exe");
        if (verifyJavaExecutable(candidate)) {
            return candidate;
        }
    }
    const QString jdkHome = qEnvironmentVariable("JDK_HOME");
    if (!jdkHome.isEmpty()) {
        const QString candidate = QDir(jdkHome).absoluteFilePath("bin/java.exe");
        if (verifyJavaExecutable(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString findJavaInCommonLocations() {
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
                if (verifyJavaExecutable(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return QString();
}

QString findJavaOnPath() {
    QString versionOutput;
    if (verifyJavaExecutable(QStringLiteral("java"), &versionOutput)) {
        return QStringLiteral("java");
    }
    return QString();
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

bool downloadJavaRuntime(const QString &destinationDir, const QString &component,
                         QString *javaPathOut) {
    QList<QUrl> urls;
    if (component == "java-runtime-gamma") {
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

    QDir().mkpath(destinationDir);
    QString tempZip = QDir(destinationDir).absoluteFilePath("java_runtime.zip");

    for (const QUrl &url : urls) {
        if (!downloadFileToLocal(url, tempZip)) {
            continue;
        }

        if (!extractZipWithPowerShell(tempZip, destinationDir)) {
            continue;
        }
        QFile::remove(tempZip);

        QString javaPath = findExtractedJava(destinationDir);
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
    const QString javaDir = QCoreApplication::applicationDirPath() + "/.minecraft/java";

    QDir dir(javaDir);
    if (!dir.exists()) {
        return versions;
    }

    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subdir : subdirs) {
        QString versionPath = dir.absoluteFilePath(subdir);
        QString javaExe = QDir(versionPath).absoluteFilePath("bin/java.exe");

        if (QFileInfo(javaExe).exists()) {
            // Try to get version from java -version
            QString versionOutput;
            if (runProcess(javaExe, {"-version"}, nullptr, &versionOutput, 5000)) {
                // Parse version, e.g., "java version "1.8.0_412"" or "openjdk version "21.0.3""
                QRegularExpression regex("version \"([^\"]+)\"");
                QRegularExpressionMatch match = regex.match(versionOutput);
                if (match.hasMatch()) {
                    QString versionStr = match.captured(1);
                    int majorVersion = 0;
                    if (versionStr.startsWith("1.")) {
                        // Java 8: 1.8.0_412 -> 8
                        majorVersion = versionStr.split(".")[1].toInt();
                    } else {
                        // Java 9+: 21.0.3 -> 21
                        majorVersion = versionStr.split(".")[0].toInt();
                    }
                    if (majorVersion > 0) {
                        versions[majorVersion] = javaExe;
                    }
                }
            }
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
    for (const QJsonValue &asset : assets) {
        QJsonObject assetObj = asset.toObject();
        QJsonObject binary = assetObj["binary"].toObject();

        QString os = binary["os"].toString();
        QString arch = binary["architecture"].toString();
        QString imageType = binary["image_type"].toString();

        if (os == "windows" && arch == "x64" && imageType == "jre") {
            QJsonObject package = binary["package"].toObject();
            downloadUrl = package["link"].toString();
            qDebug() << "Found Windows x64 JRE download URL:" << downloadUrl;
            break;
        }
    }

    if (downloadUrl.isEmpty()) {
        qDebug() << "No suitable Windows x64 JRE found in Adoptium API response";
        return false;
    }

    // Download the zip
    const QString downloadDir =
        QCoreApplication::applicationDirPath() + QString("/.minecraft/java/%1").arg(majorVersion);
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
 * Checks if the currently configured Java matches the required major version.
 * If not, scans local installations, environment variables, and downloads
 * the appropriate Java runtime from Mojang if necessary.
 *
 * @param majorVersion Required Java major version (e.g., 8, 11, 17, 21)
 * @return true if appropriate Java is available and configured, false otherwise
 */
bool GlobalJavaSettings::ensureJavaForVersion(int majorVersion) {
    qDebug() << "Ensuring Java for version:" << majorVersion;

    // First, check if we already have the correct version configured
    if (!m_javaPath.isEmpty() && verifyJavaExecutable(m_javaPath)) {
        // Check if the configured Java matches the required version
        QString versionOutput;
        if (verifyJavaExecutable(m_javaPath, &versionOutput)) {
            int currentVersion = parseJavaMajorVersion(versionOutput);
            if (currentVersion == majorVersion) {
                qDebug() << "Using configured Java version" << currentVersion
                         << "at:" << m_javaPath;
                return true;
            } else {
                qDebug() << "Configured Java version" << currentVersion
                         << "does not match required version" << majorVersion;
            }
        }
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
    QString candidate = findJavaInEnvironment();
    if (!candidate.isEmpty()) {
        // TODO: Verify version matches requirement before using
        qDebug() << "Found Java in environment:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaOnPath();
    if (!candidate.isEmpty()) {
        // TODO: Verify version matches requirement before using
        qDebug() << "Found Java on PATH:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaInCommonLocations();
    if (!candidate.isEmpty()) {
        // TODO: Verify version matches requirement before using
        qDebug() << "Found Java in common locations:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    // Download from Mojang
    qDebug() << "No matching Java found, downloading version" << majorVersion << "from Mojang...";
    if (downloadJavaFromMojang(majorVersion)) {
        qDebug() << "Downloaded Java version" << majorVersion;
        return true;
    }

    qDebug() << "Failed to obtain Java version" << majorVersion;
    return false;
}
