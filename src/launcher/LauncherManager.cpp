/**
 * @file LauncherManager.cpp
 * @brief 处理 Minecraft 游戏启动和 JVM 参数生成。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 使用 version.json 依赖和平台规定以避免 classpath 冲突。
 *
 * 这个 class 用于使用正确的 JVM 参数、 classpath 构建、特定平台 library 处理、启动 Minecraft。
 */

#include "LauncherManager.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>

#include "../settings/GlobalJavaSettings.hpp"

LauncherManager::LauncherManager(QObject *parent)
    : QObject(parent), m_gameProcess(new QProcess(this)) {
    // 连接用于监测进程和输出处理的信号的过程
    connect(m_gameProcess, &QProcess::readyReadStandardOutput, this,
            &LauncherManager::handleReadyReadStandardOutput);
    connect(m_gameProcess, &QProcess::readyReadStandardError, this,
            &LauncherManager::handleReadyReadStandardError);
    connect(m_gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &LauncherManager::handleProcessFinished);
}

bool LauncherManager::checkPreLaunch(const QString &versionId, QStringList *errors) const {
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

    // 为特定版本检查所需的游戏文件
    const QString versionJarPath = QString("%1/versions/%2/%2.jar").arg(basePath, versionId);
    const QFileInfo jarInfo(versionJarPath);
    if (!jarInfo.exists()) {
        if (errors) {
            errors->append(tr("缺少客户端 JAR 文件：%1").arg(versionJarPath));
        }
        return false;
    }

    // 检查 libraries 文件夹
    const QString librariesPath = QString("%1/libraries").arg(basePath);
    const QDir librariesDir(librariesPath);
    if (!librariesDir.exists() ||
        librariesDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        if (errors) {
            errors->append(tr("libraries 文件夹为空或不存在"));
        }
        return false;
    }

    // 检查 assets 文件夹
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
QStringList LauncherManager::generateJvmArguments(const QString &token, const QString &versionId,
                                                  const QString &assetIndexId,
                                                  const QJsonObject &versionJson,
                                                  const GlobalJavaSettings &settings,
                                                  bool offlineMode) const {
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
    // 启用对 Java 25+ 的原生兼容性
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
    // 先添加主版本 JAR 文件
    addClasspathPath(QStringLiteral("versions/%1/%1.jar").arg(versionId), classpath);

    // 根据 version.json library 依赖构建 classpath
    if (!versionJson.isEmpty()) {
        auto libs = versionJson[QStringLiteral("libraries")].toArray();
        for (const auto &value : libs) {
            auto libObj = value.toObject();
            // Skip libraries not allowed on this platform
            // 这个平台不允许跳过 libraries ？？//需要进一步完善注释
            if (!libraryAllowed(libObj)) {
                continue;
            }

            auto downloads = libObj[QStringLiteral("downloads")].toObject();
            auto artifact = downloads[QStringLiteral("artifact")].toObject();
            QString artifactPath = artifact[QStringLiteral("path")].toString();
            if (!artifactPath.isEmpty()) {
                addClasspathPath(
                    QDir::toNativeSeparators(QStringLiteral("libraries/%1").arg(artifactPath)),
                    classpath);
            }

            auto classifiers = downloads[QStringLiteral("classifiers")].toObject();
            auto natives = libObj[QStringLiteral("natives")].toObject();
            if (!natives.isEmpty()) {
                QString classifierKey = natives[osName].toString();
                if (!classifierKey.isEmpty()) {
                    auto classifierInfo = classifiers[classifierKey].toObject();
                    QString classifierPath = classifierInfo[QStringLiteral("path")].toString();
                    if (!classifierPath.isEmpty()) {
                        addClasspathPath(QDir::toNativeSeparators(
                                             QStringLiteral("libraries/%1").arg(classifierPath)),
                                         classpath);
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
        // 离线模式：使用虚假登录证书
        QString fakePlayerName =
            "Player" + QString::number(QDateTime::currentMSecsSinceEpoch() % 1000);
        QString fakeUuid = QUuid::createUuid().toString().remove('{').remove('}');

        qDebug() << "Offline mode - Player:" << fakePlayerName << "UUID:" << fakeUuid;

        args << QStringLiteral("--username") << fakePlayerName << QStringLiteral("--version")
             << versionId << QStringLiteral("--gameDir") << QString("versions/%1").arg(versionId)
             << QStringLiteral("--assetsDir") << QStringLiteral("assets")
             << QStringLiteral("--assetIndex") << assetIndexId << QStringLiteral("--uuid")
             << fakeUuid << QStringLiteral("--accessToken")
             << QStringLiteral("0")  // 离线模式使用的虚假令牌
             << QStringLiteral("--userType") << QStringLiteral("legacy");
    } else {
        // 正版登录：使用真实登录证书
        qDebug() << "Token length:" << token.length();
        args << QStringLiteral("-Dminecraft.accessToken=%1").arg(token);
        args << QStringLiteral("-Dminecraft.version=%1").arg(versionId);

        args << QStringLiteral("--username") << QStringLiteral("Player")
             << QStringLiteral("--version") << versionId << QStringLiteral("--gameDir")
             << QString("versions/%1").arg(versionId) << QStringLiteral("--assetsDir")
             << QStringLiteral("assets") << QStringLiteral("--assetIndex") << assetIndexId
             << QStringLiteral("--uuid")
             << QStringLiteral("00000000-0000-0000-0000-000000000000")  // 此处使用从验证获取的 uuid
             << QStringLiteral("--accessToken") << token << QStringLiteral("--userType")
             << QStringLiteral("msa");
    }

    qDebug() << "Total arguments count:" << args.length();
    return args;
}

void LauncherManager::startGame(const QStringList &arguments, GlobalJavaSettings &settings,
                                const QString &versionId) {
    if (m_gameProcess->state() != QProcess::NotRunning) {
        qWarning() << "Game process is already running";
        emit processError(tr("游戏进程已在运行"));
        return;
    }

    const QString program =
        settings.javaPath().isEmpty() ? QStringLiteral("java") : settings.javaPath();

    // 记录启动信息
    qDebug() << "=== Game Launch Started ===";
    qDebug() << "Java executable:" << program;
    qDebug() << "JVM arguments count:" << arguments.length();

    // 检测 Java 可执行文件存在（仅限绝对路径）
    const QFileInfo javaInfo(program);
    if (!javaInfo.exists()) {
        qWarning() << "Java executable not found at:" << program;
        emit processError(tr("Java可执行文件未找到：%1").arg(program));
        return;
    }
    qDebug() << "Java path verified:" << javaInfo.absoluteFilePath();

    // 记录部分重要参数
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
    // 将工作路径设定为 .minecraft 文件夹以进行正确的文件‘决议’ （需要重新翻译）
    QString minecraftDir = QCoreApplication::applicationDirPath() + "/.minecraft";
    QDir().mkpath(minecraftDir);
    m_gameProcess->setWorkingDirectory(minecraftDir);
    qDebug() << "Working directory:" << minecraftDir;

    // 为这个版本创建独立的路径
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

void LauncherManager::handleReadyReadStandardOutput() {
    const QString output = QString::fromLocal8Bit(m_gameProcess->readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        qDebug().noquote() << "[Game Output]" << output;
        emit processOutput(output);
    }
}

void LauncherManager::handleReadyReadStandardError() {
    const QString error = QString::fromLocal8Bit(m_gameProcess->readAllStandardError()).trimmed();
    if (!error.isEmpty()) {
        qWarning().noquote() << "[Game Error]" << error;
        emit processError(error);
    }
}

void LauncherManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qDebug() << "=== Game Process Finished ===";
    qDebug() << "Exit code:" << exitCode;
    qDebug() << "Exit status:" << (exitStatus == QProcess::NormalExit ? "Normal" : "Crash");

    // 检查剩余的输出
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
