/**
 * @file LauncherManager.cpp
 * @brief Manages Minecraft game launching and JVM argument generation.
 *
 * This class handles the process of starting Minecraft with appropriate JVM arguments,
 * classpath construction, and platform-specific library handling.
 */

#include "LauncherManager.hpp"
#include "../settings/GlobalJavaSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSysInfo>
#include <QUuid>

LauncherManager::LauncherManager(QObject *parent)
    : QObject(parent)
    , m_gameProcess(new QProcess(this))
{
    // Connect process signals for output handling and process monitoring
    connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, &LauncherManager::handleReadyReadStandardOutput);
    connect(m_gameProcess, &QProcess::readyReadStandardError, this, &LauncherManager::handleReadyReadStandardError);
    connect(m_gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &LauncherManager::handleProcessFinished);
}

bool LauncherManager::checkPreLaunch(const QString &versionId, QStringList *errors) const
{
    if (errors) {
        errors->clear();
    }

    const QString basePath = QCoreApplication::applicationDirPath() + "/.minecraft";
    if (basePath.isEmpty()) {
        if (errors) {
            errors->append(tr("无法获取 Minecraft 目录。"));
        }
        return false;
    }

    const QFileInfo info(basePath);
    if (!info.exists() && !QDir().mkpath(basePath)) {
        if (errors) {
            errors->append(tr("无法创建 Minecraft 目录：%1").arg(basePath));
        }
        return false;
    }

    // Check for required game files for the specific version
    const QString versionJarPath = QString("%1/versions/%2/%2.jar").arg(basePath, versionId);
    const QFileInfo jarInfo(versionJarPath);
    if (!jarInfo.exists()) {
        if (errors) {
            errors->append(tr("缺少客户端 JAR 文件：%1").arg(versionJarPath));
        }
        return false;
    }

    // Check libraries folder
    const QString librariesPath = QString("%1/libraries").arg(basePath);
    const QDir librariesDir(librariesPath);
    if (!librariesDir.exists() || librariesDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        if (errors) {
            errors->append(tr("libraries 文件夹为空或不存在"));
        }
        return false;
    }

    // Check assets folder
    const QString assetsPath = QString("%1/assets").arg(basePath);
    const QDir assetsDir(assetsPath);
    if (!assetsDir.exists()) {
        if (errors) {
            errors->append(tr("assets 文件夹不存在"));
        }
        return false;
    }

    return true;
}

/**
 * @brief Generates JVM arguments for Minecraft launch.
 *
 * Constructs the complete set of JVM arguments including memory settings,
 * classpath, system properties, and game-specific parameters. Uses version.json
 * dependencies to build classpath and applies platform-specific library filtering.
 *
 * @param token Authentication token for online mode
 * @param versionId Minecraft version identifier
 * @param assetIndexId Asset index identifier
 * @param versionJson Version manifest JSON object containing library dependencies
 * @param settings Java settings (memory, extra args, etc.)
 * @param offlineMode Whether to run in offline mode with fake credentials
 * @return QStringList of JVM arguments ready for QProcess
 */
QStringList LauncherManager::generateJvmArguments(const QString &token, const QString &versionId, const QString &assetIndexId, const QJsonObject &versionJson, const GlobalJavaSettings &settings, bool offlineMode) const
{
    QStringList args;
    
    qDebug() << "=== Generating JVM Arguments ===";
    qDebug() << "Memory settings: -Xms" << settings.xms() << "m -Xmx" << settings.xmx() << "m";
    qDebug() << "Offline mode:" << offlineMode;
    
    args << QStringLiteral("-Xms%1m").arg(settings.xms());
    args << QStringLiteral("-Xmx%1m").arg(settings.xmx());
    
    if (!settings.extraJvmArgs().isEmpty()) {
        qDebug() << "Extra JVM args:" << settings.extraJvmArgs();
        args << settings.extraJvmArgs().split(' ', Qt::SkipEmptyParts);
    }

    args << QStringLiteral("-Dlog4j2.formatMsgNoLookups=true");
    // Enable native access for Java 25+ compatibility
    args << QStringLiteral("--enable-native-access=ALL-UNNAMED");
    args << QStringLiteral("-Djava.net.preferIPv4Stack=true");
    
    const auto osName = []() {
        const QString product = QSysInfo::productType().toLower();
        if (product.contains("windows")) {
            return QStringLiteral("windows");
        }
        if (product.contains("mac") || product.contains("osx") || product.contains("darwin")) {
            return QStringLiteral("osx");
        }
        if (product.contains("linux")) {
            return QStringLiteral("linux");
        }
        return QStringLiteral("windows");
    }();

    const auto libraryAllowed = [&](const QJsonObject &libObj) -> bool {
        auto rules = libObj[QStringLiteral("rules")].toArray();
        if (rules.isEmpty()) {
            return true;
        }

        bool allowed = false;
        for (const auto &ruleValue : rules) {
            auto ruleObj = ruleValue.toObject();
            QString action = ruleObj[QStringLiteral("action")].toString();
            if (action.isEmpty()) {
                continue;
            }

            auto osObj = ruleObj[QStringLiteral("os")].toObject();
            bool matches = osObj.isEmpty();
            if (!matches) {
                QString ruleName = osObj[QStringLiteral("name")].toString().toLower();
                if (!ruleName.isEmpty()) {
                    matches = (ruleName == osName);
                }
            }

            if (!matches) {
                continue;
            }

            if (action == QStringLiteral("allow")) {
                allowed = true;
            } else if (action == QStringLiteral("disallow") || action == QStringLiteral("deny")) {
                allowed = false;
            }
        }
        return allowed;
    };

    const auto addClasspathPath = [&](const QString &path, QStringList &classpath) {
        if (!path.isEmpty() && !classpath.contains(path)) {
            classpath << path;
        }
    };

    QStringList classpath;
    // Add the main version JAR first
    addClasspathPath(QStringLiteral("versions/%1/%1.jar").arg(versionId), classpath);

    // Build classpath from version.json library dependencies
    if (!versionJson.isEmpty()) {
        auto libs = versionJson[QStringLiteral("libraries")].toArray();
        for (const auto &value : libs) {
            auto libObj = value.toObject();
            // Skip libraries not allowed on this platform
            if (!libraryAllowed(libObj)) {
                continue;
            }

            auto downloads = libObj[QStringLiteral("downloads")].toObject();
            auto artifact = downloads[QStringLiteral("artifact")].toObject();
            QString artifactPath = artifact[QStringLiteral("path")].toString();
            if (!artifactPath.isEmpty()) {
                addClasspathPath(QDir::toNativeSeparators(QStringLiteral("libraries/%1").arg(artifactPath)), classpath);
            }

            auto classifiers = downloads[QStringLiteral("classifiers")].toObject();
            auto natives = libObj[QStringLiteral("natives")].toObject();
            if (!natives.isEmpty()) {
                QString classifierKey = natives[osName].toString();
                if (!classifierKey.isEmpty()) {
                    auto classifierInfo = classifiers[classifierKey].toObject();
                    QString classifierPath = classifierInfo[QStringLiteral("path")].toString();
                    if (!classifierPath.isEmpty()) {
                        addClasspathPath(QDir::toNativeSeparators(QStringLiteral("libraries/%1").arg(classifierPath)), classpath);
                    }
                }
            }
        }
    } else {
        qDebug() << "Warning: versionJson is empty, classpath will include only the version JAR.";
    }

    const QString classpathStr = classpath.join(";");
    qDebug() << "Classpath:" << classpathStr;
    
    args << QStringLiteral("-cp") << classpathStr;
    args << QStringLiteral("net.minecraft.client.main.Main");
    
    if (offlineMode) {
        // Offline mode: use fake credentials
        QString fakePlayerName = "Player" + QString::number(QDateTime::currentMSecsSinceEpoch() % 1000);
        QString fakeUuid = QUuid::createUuid().toString().remove('{').remove('}');
        
        qDebug() << "Offline mode - Player:" << fakePlayerName << "UUID:" << fakeUuid;
        
        args << QStringLiteral("--username") << fakePlayerName
             << QStringLiteral("--version") << versionId
             << QStringLiteral("--gameDir") << QString("versions/%1").arg(versionId)
             << QStringLiteral("--assetsDir") << QStringLiteral("assets")
             << QStringLiteral("--assetIndex") << assetIndexId
             << QStringLiteral("--uuid") << fakeUuid
             << QStringLiteral("--accessToken") << QStringLiteral("0")  // Fake token for offline mode
             << QStringLiteral("--userType") << QStringLiteral("legacy");
    } else {
        // Online mode: use real credentials
        qDebug() << "Token length:" << token.length();
        args << QStringLiteral("-Dminecraft.accessToken=%1").arg(token);
        args << QStringLiteral("-Dminecraft.version=%1").arg(versionId);
        
        args << QStringLiteral("--username") << QStringLiteral("Player")
             << QStringLiteral("--version") << versionId
             << QStringLiteral("--gameDir") << QString("versions/%1").arg(versionId)
             << QStringLiteral("--assetsDir") << QStringLiteral("assets")
             << QStringLiteral("--assetIndex") << assetIndexId
             << QStringLiteral("--uuid") << QStringLiteral("00000000-0000-0000-0000-000000000000")  // This should be real UUID from auth
             << QStringLiteral("--accessToken") << token
             << QStringLiteral("--userType") << QStringLiteral("msa");
    }
    
    qDebug() << "Total arguments count:" << args.length();
    return args;
}

void LauncherManager::startGame(const QStringList &arguments, GlobalJavaSettings &settings, const QString &versionId)
{
    if (m_gameProcess->state() != QProcess::NotRunning) {
        qWarning() << "Game process is already running";
        emit processError(tr("游戏进程已在运行"));
        return;
    }

    const QString program = settings.javaPath().isEmpty() ? QStringLiteral("java") : settings.javaPath();
    
    // Log startup information
    qDebug() << "=== Game Launch Started ===";
    qDebug() << "Java executable:" << program;
    qDebug() << "JVM arguments count:" << arguments.length();
    
    // Check if Java executable exists (only for absolute paths)
    const QFileInfo javaInfo(program);
    if (!javaInfo.exists()) {
        qWarning() << "Java executable not found at:" << program;
        emit processError(tr("Java可执行文件未找到：%1").arg(program));
        return;
    }
    qDebug() << "Java path verified:" << javaInfo.absoluteFilePath();
    
    // Log some of the important arguments
    for (int i = 0; i < arguments.length(); ++i) {
        const QString &arg = arguments.at(i);
        if (arg.startsWith("-D")) {
            qDebug() << "  " << arg.left(50) << (arg.length() > 50 ? "..." : "");
        } else if (arg == "-cp" || arg == "-classpath") {
            qDebug() << "  " << arg;
            if (static_cast<int>(i + 1) < arguments.length()) {
                ++i;
                qDebug() << "    " << arguments.at(i);
            }
        }
    }
    
    m_gameProcess->setProgram(program);
    m_gameProcess->setArguments(arguments);
    
    // Set working directory to .minecraft folder for proper file resolution
    QString minecraftDir = QCoreApplication::applicationDirPath() + "/.minecraft";
    QDir().mkpath(minecraftDir);
    m_gameProcess->setWorkingDirectory(minecraftDir);
    qDebug() << "Working directory:" << minecraftDir;
    
    // Create isolated directory for this version
    QString isolatedPath = minecraftDir + "/versions/" + versionId;
    QDir().mkpath(isolatedPath);
    qDebug() << "Isolated game directory:" << isolatedPath;
    
    qDebug() << "Starting java process...";
    m_gameProcess->start();
    if (!m_gameProcess->waitForStarted(3000)) {
        qWarning() << "Failed to start game process";
        qWarning() << "Error:" << m_gameProcess->errorString();
        emit processError(tr("无法启动游戏进程：%1").arg(m_gameProcess->errorString()));
        return;
    }
    
    qDebug() << "Java process started, PID:" << m_gameProcess->processId();
    emit processOutput(tr("游戏已启动（进程ID：%1）").arg(m_gameProcess->processId()));
}

void LauncherManager::handleReadyReadStandardOutput()
{
    const QString output = QString::fromLocal8Bit(m_gameProcess->readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        qDebug().noquote() << "[Game Output]" << output;
        emit processOutput(output);
    }
}

void LauncherManager::handleReadyReadStandardError()
{
    const QString error = QString::fromLocal8Bit(m_gameProcess->readAllStandardError()).trimmed();
    if (!error.isEmpty()) {
        qWarning().noquote() << "[Game Error]" << error;
        emit processError(error);
    }
}

void LauncherManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "=== Game Process Finished ===";
    qDebug() << "Exit code:" << exitCode;
    qDebug() << "Exit status:" << (exitStatus == QProcess::NormalExit ? "Normal" : "Crash");
    
    // Check for any remaining output
    const QByteArray remainingOutput = m_gameProcess->readAllStandardOutput();
    const QByteArray remainingError = m_gameProcess->readAllStandardError();
    
    if (!remainingOutput.isEmpty()) {
        const QString output = QString::fromLocal8Bit(remainingOutput).trimmed();
        qDebug() << "Remaining output:" << output;
        emit processOutput(output);
    }
    
    if (!remainingError.isEmpty()) {
        const QString error = QString::fromLocal8Bit(remainingError).trimmed();
        qDebug() << "Remaining error:" << error;
        emit processError(error);
    }
    
    emit gameFinished(exitCode, exitStatus);
}
