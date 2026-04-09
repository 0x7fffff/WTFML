#include "LauncherManager.h"
#include "../settings/GlobalJavaSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QDirIterator>
#include <QUuid>

LauncherManager::LauncherManager(QObject *parent)
    : QObject(parent)
    , m_gameProcess(new QProcess(this))
{
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

QStringList LauncherManager::generateJvmArguments(const QString &token, const QString &versionId, const QString &assetIndexId, const GlobalJavaSettings &settings, bool offlineMode) const
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
    args << QStringLiteral("-Djava.library.path=./natives");
    args << QStringLiteral("-Djava.net.preferIPv4Stack=true");
    
    // Build classpath with version-specific jar and libraries
    QStringList classpath;
    classpath << QString("versions/%1/%1.jar").arg(versionId);
    
    // Add all jar files from libraries directory
    const QString librariesPath = QCoreApplication::applicationDirPath() + "/.minecraft/libraries";
    const QDir librariesDir(librariesPath);
    if (librariesDir.exists()) {
        QDirIterator it(librariesPath, QStringList() << "*.jar", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QString relativePath = QDir(librariesPath).relativeFilePath(it.filePath());
            classpath << QString("libraries/%1").arg(relativePath);
        }
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
             << QStringLiteral("--gameDir") << QStringLiteral(".")
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
             << QStringLiteral("--gameDir") << QStringLiteral(".")
             << QStringLiteral("--assetsDir") << QStringLiteral("assets")
             << QStringLiteral("--assetIndex") << assetIndexId
             << QStringLiteral("--uuid") << QStringLiteral("00000000-0000-0000-0000-000000000000")  // This should be real UUID from auth
             << QStringLiteral("--accessToken") << token
             << QStringLiteral("--userType") << QStringLiteral("msa");
    }
    
    qDebug() << "Total arguments count:" << args.length();
    return args;
}

void LauncherManager::startGame(const QStringList &arguments, GlobalJavaSettings &settings)
{
    if (m_gameProcess->state() != QProcess::NotRunning) {
        qWarning() << "Game process is already running";
        emit processError(tr("游戏进程已在运行"));
        return;
    }

    if (!settings.ensureJavaAvailable()) {
        qWarning() << "No valid Java installation found.";
        emit processError(tr("未找到有效的 Java 环境，正在下载或请手动安装。"));
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
