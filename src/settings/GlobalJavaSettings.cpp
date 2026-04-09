#include "GlobalJavaSettings.h"

#include <QSettings>
#include <QProcess>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTemporaryFile>
#include <QCoreApplication>

namespace {
static constexpr const char kJavaPath[] = "Java/Path";
static constexpr const char kXmx[] = "Java/Xmx";
static constexpr const char kXms[] = "Java/Xms";
static constexpr const char kExtraArgs[] = "Java/ExtraArgs";

bool runProcess(const QString &program, const QStringList &arguments, QString *stdOut = nullptr, QString *stdErr = nullptr, int timeoutMs = 5000)
{
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

bool verifyJavaExecutable(const QString &path, QString *versionOut = nullptr)
{
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

QString findJavaInEnvironment()
{
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

QString findJavaInCommonLocations()
{
    const QStringList rootDirs = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("ProgramW6432")
    };

    for (const QString &rootDirPath : rootDirs) {
        if (rootDirPath.isEmpty()) {
            continue;
        }
        QDir rootDir(rootDirPath);
        if (!rootDir.exists()) {
            continue;
        }

        QDirIterator it(rootDir.absolutePath(), QStringList() << "java.exe", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString candidate = it.next();
            if (candidate.contains("Java", Qt::CaseInsensitive)
                || candidate.contains("jdk", Qt::CaseInsensitive)
                || candidate.contains("jre", Qt::CaseInsensitive)
                || candidate.contains("temurin", Qt::CaseInsensitive)
                || candidate.contains("adoptium", Qt::CaseInsensitive)
                || candidate.contains("corretto", Qt::CaseInsensitive)
                || candidate.contains("microsoft", Qt::CaseInsensitive)
                || candidate.contains("zulu", Qt::CaseInsensitive)) {
                if (verifyJavaExecutable(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return QString();
}

QString findJavaOnPath()
{
    QString versionOutput;
    if (verifyJavaExecutable(QStringLiteral("java"), &versionOutput)) {
        return QStringLiteral("java");
    }
    return QString();
}

bool extractZipWithPowerShell(const QString &zipFile, const QString &targetDir)
{
    QString command = QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(zipFile, targetDir);
    QString stdOut, stdErr;
    bool ok = runProcess(QStringLiteral("powershell"), {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), command}, &stdOut, &stdErr, 60000);
    Q_UNUSED(stdOut);
    return ok;
}

QString findExtractedJava(const QString &rootDir)
{
    QDirIterator it(rootDir, QStringList() << "java.exe", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString candidate = it.next();
        if (verifyJavaExecutable(candidate)) {
            return candidate;
        }
    }
    return QString();
}

bool downloadFileToLocal(const QUrl &url, const QString &destinationPath)
{
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

bool downloadJavaRuntime(const QString &destinationDir, QString *javaPathOut)
{
    const QList<QUrl> urls = {
        QUrl("https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.3+9/OpenJDK21U-jre_x64_windows_hotspot_21.0.3_9.zip"),
        QUrl("https://download.bell-sw.com/java/21.0.2+14/bellsoft-jre21.0.2+14-win-amd64.zip")
    };

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
}

GlobalJavaSettings::GlobalJavaSettings(QObject *parent)
    : QObject(parent)
    , m_xmx(4096)  // 4GB
    , m_xms(1024)  // 1GB
    , m_extraJvmArgs(QStringLiteral("-XX:+UseG1GC"))
{
    load();
}

QString GlobalJavaSettings::javaPath() const
{
    return m_javaPath;
}

void GlobalJavaSettings::setJavaPath(const QString &path)
{
    m_javaPath = path;
}

int GlobalJavaSettings::xmx() const
{
    return m_xmx;
}

void GlobalJavaSettings::setXmx(int mbytes)
{
    m_xmx = qMax(256, mbytes);
}

int GlobalJavaSettings::xms() const
{
    return m_xms;
}

void GlobalJavaSettings::setXms(int mbytes)
{
    m_xms = qMax(128, mbytes);
}

QString GlobalJavaSettings::extraJvmArgs() const
{
    return m_extraJvmArgs;
}

void GlobalJavaSettings::setExtraJvmArgs(const QString &args)
{
    m_extraJvmArgs = args;
}

void GlobalJavaSettings::load()
{
    QSettings settings;
    m_javaPath = settings.value(kJavaPath).toString();
    m_xmx = settings.value(kXmx, m_xmx).toInt();
    m_xms = settings.value(kXms, m_xms).toInt();
    m_extraJvmArgs = settings.value(kExtraArgs, m_extraJvmArgs).toString();
}

void GlobalJavaSettings::save() const
{
    QSettings settings;
    settings.setValue(kJavaPath, m_javaPath);
    settings.setValue(kXmx, m_xmx);
    settings.setValue(kXms, m_xms);
    settings.setValue(kExtraArgs, m_extraJvmArgs);
}

bool GlobalJavaSettings::ensureJavaAvailable()
{
    qDebug() << "Checking for Java...";

    if (!m_javaPath.isEmpty() && verifyJavaExecutable(m_javaPath)) {
        qDebug() << "Using configured Java:" << m_javaPath;
        return true;
    }

    QString candidate;
    candidate = findJavaInEnvironment();
    if (!candidate.isEmpty()) {
        qDebug() << "Found Java in environment:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaOnPath();
    if (!candidate.isEmpty()) {
        qDebug() << "Found Java on PATH:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    candidate = findJavaInCommonLocations();
    if (!candidate.isEmpty()) {
        qDebug() << "Found Java in common locations:" << candidate;
        m_javaPath = candidate;
        save();
        return true;
    }

    qDebug() << "No Java found, attempting download...";
    const QString downloadDir = QCoreApplication::applicationDirPath() + "/.minecraft/java";
    QString downloadedJava;
    if (downloadJavaRuntime(downloadDir, &downloadedJava)) {
        if (!downloadedJava.isEmpty() && verifyJavaExecutable(downloadedJava)) {
            qDebug() << "Downloaded Java:" << downloadedJava;
            m_javaPath = downloadedJava;
            save();
            return true;
        } else {
            qDebug() << "Downloaded Java verification failed:" << downloadedJava;
        }
    } else {
        qDebug() << "Java download failed.";
    }

    qDebug() << "No valid Java installation found.";
    return false;
}
