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
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include "../settings/GlobalJavaSettings.hpp"
#include "../util/AppPaths.hpp"
#include "../util/ZipUtil.hpp"
#include "LibraryRules.hpp"
#include "VersionArguments.hpp"

namespace {

/**
 * @brief 取某个版本解压 natives 的目标目录。
 * @param versionId Minecraft version identifier.
 * @return 返回 versions/<id>/natives 的绝对路径。
 */
QString nativesDirectory(const QString &versionId) {
    return AppPaths::nativesDir(versionId);
}

/**
 * @brief 判断目录里是否已经有解压出来的 DLL。
 * @param dir 待检查的目录。
 * @return 若目录存在且至少含一个 .dll 则返回 true.
 */
bool hasNativeLibraries(const QString &dir) {
    return !QDir(dir).entryList(QStringList() << QStringLiteral("*.dll"), QDir::Files).isEmpty();
}

/**
 * @brief 首次为一个版本创建游戏目录时，把界面语言设成跟随系统。
 * @param gameDir 该版本的隔离游戏目录。
 *
 * 只在 options.txt 不存在时写入，所以玩家自己选过的语言永远不会被覆盖。
 * Minecraft 读取时会忽略缺失的键并用默认值补齐，因此只写一行 lang 是安全的。
 */
void ensureInitialOptions(const QString &gameDir) {
    const QString optionsPath = QDir(gameDir).absoluteFilePath(QStringLiteral("options.txt"));
    if (QFileInfo::exists(optionsPath)) {
        return;
    }
    if (!QDir().mkpath(gameDir)) {
        return;
    }

    // QLocale::system().name() 形如 "zh_CN"，而 Minecraft 的语言代码是全小写的 "zh_cn"，
    // 正好和资源包里的 lang/<code>.json 文件名对应。
    QString lang = QLocale::system().name().toLower();
    // "C" 区域（从未设置过 locale）或者没有国家/地区后缀时，没有对应的语言文件，
    // 这时只能用 Minecraft 内置在客户端 jar 里的 en_us。
    if (lang.isEmpty() || lang == QLatin1String("c") || !lang.contains(QLatin1Char('_'))) {
        lang = QStringLiteral("en_us");
    }

    QFile file(optionsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to create" << optionsPath << file.errorString();
        return;
    }
    file.write(QStringLiteral("lang:%1\n").arg(lang).toUtf8());
    file.close();
    qDebug() << "Created initial options.txt with lang" << lang;
}

}  // namespace

LauncherManager::LauncherManager(QObject *parent)
    : QObject(parent), m_gameProcess(new QProcess(this)) {
    // 连接用于监测进程和输出处理的信号的进程
    connect(m_gameProcess, &QProcess::readyReadStandardOutput, this,
            &LauncherManager::handleReadyReadStandardOutput);
    connect(m_gameProcess, &QProcess::readyReadStandardError, this,
            &LauncherManager::handleReadyReadStandardError);
    connect(m_gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &LauncherManager::handleProcessFinished);
}

bool LauncherManager::checkPreLaunch(const QString &versionId, QStringList *errors,
                                     const QJsonObject &versionJson) const {
    if (errors) {
        errors->clear();
    }

    const QString basePath = AppPaths::minecraftDir();
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

    // 旧格式（1.18.2 及以下）依赖解压出来的 natives，缺了只会崩在
    // UnsatisfiedLinkError，不如在这里就说清楚。1.19+ 没有 natives 字段，直接跳过。
    if (!versionJson.isEmpty()) {
        bool needsNatives = false;
        const QJsonArray libraries = versionJson[QStringLiteral("libraries")].toArray();
        for (const QJsonValue &value : libraries) {
            if (LibraryRules::nativeClassifier(value.toObject()).valid()) {
                needsNatives = true;
                break;
            }
        }

        if (needsNatives && !hasNativeLibraries(nativesDirectory(versionId))) {
            if (errors) {
                errors->append(tr("该版本需要 natives 组件，但 %1 里没有解压出 DLL。")
                                   .arg(nativesDirectory(versionId)));
            }
            return false;
        }
    }

    return true;
}

QString LauncherManager::prepareNatives(const QJsonObject &versionJson, const QString &versionId,
                                        QStringList *errors) const {
    if (versionJson.isEmpty()) {
        return QString();
    }

    const QJsonArray libraries = versionJson[QStringLiteral("libraries")].toArray();
    QList<LibraryRules::NativeClassifier> classifiers;
    for (const QJsonValue &value : libraries) {
        const LibraryRules::NativeClassifier classifier =
            LibraryRules::nativeClassifier(value.toObject());
        if (classifier.valid()) {
            classifiers.append(classifier);
        }
    }

    // 1.19+ 走这里：classifier 写在 name 里、由 LWJGL 自己从 classpath 的 jar 提取，
    // 既不需要 -Djava.library.path 也不需要解压。
    if (classifiers.isEmpty()) {
        return QString();
    }

    const QString nativesDir = nativesDirectory(versionId);
    // 守卫：上一次已经解压过就直接复用。除第一次启动外，这里几乎是零开销。
    if (hasNativeLibraries(nativesDir)) {
        return nativesDir;
    }

    const QString librariesRoot = AppPaths::libraryDir();

    QStringList jars;
    QStringList excludes;
    QStringList missing;
    for (const LibraryRules::NativeClassifier &classifier : classifiers) {
        const QString jarPath = QDir(librariesRoot).absoluteFilePath(classifier.path);
        if (!QFileInfo::exists(jarPath)) {
            missing << classifier.path;
            continue;
        }
        jars << QDir::toNativeSeparators(jarPath);
        for (const QString &exclude : classifier.excludes) {
            if (!excludes.contains(exclude)) {
                excludes << exclude;
            }
        }
    }

    if (!missing.isEmpty()) {
        if (errors) {
            errors->append(tr("natives 组件尚未下载，请先重新下载该版本。缺少：%1")
                               .arg(missing.join(QStringLiteral(", "))));
        }
        return QString();
    }

    qDebug() << "Extracting" << jars.size() << "natives archives to" << nativesDir;
    QString error;
    if (!ZipUtil::extractArchives(jars, nativesDir, excludes, &error)) {
        if (errors) {
            errors->append(tr("解压 natives 失败：%1").arg(error));
        }
        return QString();
    }

    if (!hasNativeLibraries(nativesDir)) {
        if (errors) {
            errors->append(tr("natives 解压后未找到 DLL：%1").arg(nativesDir));
        }
        return QString();
    }

    qDebug() << "Natives ready at" << nativesDir;
    return nativesDir;
}

/**
 * @brief 为 Minecraft 启动生成 JVM 参数。
 *
 * 构造完整的 JVM 参数集，包括内存设置、 classpath 、系统属性和游戏特定参数。
 * 使用 version.json 中的依赖项构建 classpath ，并应用针对特定平台的 library 过滤。
 *
 * @param token Authentication token for online mode
 * @param versionId Minecraft version identifier
 * @param assetIndexId Asset index identifier
 * @param versionJson Version manifest JSON object containing library dependencies
 * @param settings Java settings (memory, extra args, etc.)
 * @param offlineMode Whether to run in offline mode with fake credentials
 * @return 返回为 QProcess 准备的带有 JVM 参数的 QStringList
 */
QStringList LauncherManager::generateJvmArguments(const QString &token, const QString &versionId,
                                                  const QString &assetIndexId,
                                                  const QJsonObject &versionJson,
                                                  const GlobalJavaSettings &settings,
                                                  bool offlineMode, int javaMajorVersion) const {
    QStringList args;

    qDebug() << "=== Generating JVM Arguments ===";
    qDebug() << "Memory settings: -Xms" << settings.xms() << "m -Xmx" << settings.xmx() << "m";
    qDebug() << "Offline mode:" << offlineMode;
    qDebug() << "Java major version:" << javaMajorVersion;

    args << QStringLiteral("-Xms%1m").arg(settings.xms());
    args << QStringLiteral("-Xmx%1m").arg(settings.xmx());

    if (!settings.extraJvmArgs().isEmpty()) {
        qDebug() << "Extra JVM args:" << settings.extraJvmArgs();
        args << settings.extraJvmArgs().split(' ', Qt::SkipEmptyParts);
    }

    args << QStringLiteral("-Dlog4j2.formatMsgNoLookups=true");
    // --enable-native-access 是 JDK 17+ 才有的选项，用于启用对 Java 25+ 的原生兼容性。
    // Java 8（1.16.5 及以下需要）碰到它会直接 Unrecognized option 秒退，连主类都到不了，
    // 所以必须按实际使用的 Java 版本过滤。javaMajorVersion 为 0 表示调用方没传，沿用旧行为。
    if (javaMajorVersion >= 17 || javaMajorVersion <= 0) {
        args << QStringLiteral("--enable-native-access=ALL-UNNAMED");
    }
    args << QStringLiteral("-Djava.net.preferIPv4Stack=true");

    const auto addClasspathPath = [&](const QString &path, QStringList &classpath) {
        if (!path.isEmpty() && !classpath.contains(path)) {
            classpath << path;
        }
    };

    QStringList classpath;
    bool needsNativesDir = false;
    // 先添加主版本 JAR 文件
    addClasspathPath(QStringLiteral("versions/%1/%1.jar").arg(versionId), classpath);

    // 根据 version.json library 依赖构建 classpath
    if (!versionJson.isEmpty()) {
        auto libs = versionJson[QStringLiteral("libraries")].toArray();
        for (const auto &value : libs) {
            auto libObj = value.toObject();
            // 跳过不适用这个平台的库
            if (!LibraryRules::libraryAllowed(libObj)) {
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

            // NOTE: 旧格式（1.18.2 及以下）的 natives classifier jar 不进 classpath ——
            // 它们由 prepareNatives 解压到 versions/<id>/natives/，再通过
            // -Djava.library.path 提供给 LWJGL。以前把不存在的 classifier 路径塞进
            // classpath，正是这些版本崩在 UnsatisfiedLinkError 的原因。
            // 1.19+ 的库没有 natives 字段，走上面的 artifact 分支留在 classpath 上，
            // 由 LWJGL 自己从 jar 里提取，行为与改动前一致。
            if (!libObj[QStringLiteral("natives")].toObject().isEmpty()) {
                needsNativesDir = true;
            }
        }
    } else {
        qDebug() << "Warning: versionJson is empty, classpath will include only the version JAR.";
    }

    // NOTE: 必须是绝对路径。JVM 在启动早期就读取并缓存 java.library.path，
    // 相对路径的解析基准是那一刻的工作目录，不可依赖；原版启动器传的也是绝对路径。
    if (needsNativesDir) {
        const QString nativesDir = nativesDirectory(versionId);
        if (hasNativeLibraries(nativesDir)) {
            const QString nativePath = QDir::toNativeSeparators(nativesDir);
            args << QStringLiteral("-Djava.library.path=%1").arg(nativePath);
            // LWJGL 2（1.12.2 及以下）只认 java.library.path，这条是给它的冗余保险
            args << QStringLiteral("-Dorg.lwjgl.librarypath=%1").arg(nativePath);
        } else {
            qWarning() << "Natives directory is missing or empty, the game will most likely fail "
                          "with UnsatisfiedLinkError:"
                       << nativesDir;
        }
    }

    const QString classpathStr = classpath.join(";");
    qDebug() << "Classpath:" << classpathStr;

    // ------------------------------------------------------------------
    // version.json 声明的 JVM 参数（arguments.jvm）
    // ------------------------------------------------------------------
    // NOTE: 这里以前完全忽略 arguments.jvm，只发我们自己硬编码的一小串参数。
    // 26.3 正是靠 arguments.jvm 声明了 '-XX:StackShadowPages=32'（这个 JDK 的默认值
    // 是 8）：影子区不够时，编译代码在资源重载这种深栈路径上会越过影子区踩到保护页，
    // 表现为 jvm.dll 里的访问冲突 —— 没有 Java 异常、没有 hs_err、没有崩溃报告，
    // 进程直接消失。凡是 version.json 声明了的，就该按它的规则过滤后原样传给 JVM，
    // 而不是由我们维护一份会过期的参数清单。
    const QString nativesDir = QDir::toNativeSeparators(nativesDirectory(versionId));
    const QString librariesDir = AppPaths::libraryDir();

    QHash<QString, QString> placeholders;
    placeholders[QStringLiteral("natives_directory")] = nativesDir;
    placeholders[QStringLiteral("library_directory")] = QDir::toNativeSeparators(librariesDir);
    placeholders[QStringLiteral("launcher_name")] = QStringLiteral("WTFML");
    placeholders[QStringLiteral("launcher_version")] =
        QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("1.0")
            : QCoreApplication::applicationVersion();
    placeholders[QStringLiteral("classpath")] = classpathStr;
    placeholders[QStringLiteral("classpath_separator")] = QStringLiteral(";");
    placeholders[QStringLiteral("version_name")] = versionId;

    const QStringList declared =
        VersionArguments::expand(versionJson.value(QStringLiteral("arguments"))
                                     .toObject()
                                     .value(QStringLiteral("jvm"))
                                     .toArray(),
                                 placeholders);

    // 同一条属性只留一份：version.json 声明了就以它为准。值与我们完全相同时也归到
    // 这里，免得同一个 -D 在命令行上出现两次。
    if (VersionArguments::declaresPrefix(declared, QStringLiteral("--enable-native-access="))) {
        args.removeAll(QStringLiteral("--enable-native-access=ALL-UNNAMED"));
    }
    if (VersionArguments::declaresPrefix(declared, QStringLiteral("-Djava.library.path="))) {
        for (int i = args.size() - 1; i >= 0; --i) {
            if (args.at(i).startsWith(QStringLiteral("-Djava.library.path="))) {
                args.removeAt(i);
            }
        }
    }
    qDebug() << "Declared JVM arguments from version.json:" << declared;
    args << declared;

    // 声明的参数引用了 natives 目录就把它建出来：26.3 把它的 /lwjgl /jna /netty /java
    // 子目录当作 LWJGL/JNA/netty 的提取目录，原版启动器在启动前总会创建这个目录。
    // 建个空目录本身无害（prepareNatives 只认目录里有没有 .dll）。
    if (!QDir(nativesDir).exists()) {
        for (const QString &arg : declared) {
            if (arg.contains(nativesDir, Qt::CaseInsensitive)) {
                QDir().mkpath(nativesDir);
                break;
            }
        }
    }

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

bool LauncherManager::startGame(const QStringList &arguments, GlobalJavaSettings &settings,
                                const QString &versionId) {
    // 返回值 = "进程真的起来了"。三条早退路径都必须 return false：调用方拿它当
    // "这次算不算启动成功"的判据（据此记住上次启动的版本），任何一条漏掉都会把
    // 失败当成功记下来。这个函数里**每一条 return 都要想一遍返回值**。
    if (m_gameProcess->state() != QProcess::NotRunning) {
        qWarning() << "Game process is already running";
        emit processError(tr("游戏进程已在运行"));
        return false;
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
        return false;
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

    // 将工作目录设为 .minecraft 文件夹以确保文件能够正确解析
    QString minecraftDir = AppPaths::minecraftDir();
    QDir().mkpath(minecraftDir);
    m_gameProcess->setWorkingDirectory(minecraftDir);
    qDebug() << "Working directory:" << minecraftDir;

    // 为这个版本创建独立的路径
    QString isolatedPath = AppPaths::versionDir(versionId);
    QDir().mkpath(isolatedPath);
    // 第一次启动这个版本时，让游戏界面语言跟随系统
    ensureInitialOptions(isolatedPath);
    qDebug() << "Isolated game directory:" << isolatedPath;

    qDebug() << "Starting java process...";
    m_gameProcess->start();
    if (!m_gameProcess->waitForStarted(3000)) {
        qWarning() << "Failed to start game process";
        qWarning() << "Error:" << m_gameProcess->errorString();
        emit processError(tr("无法启动游戏进程：%1").arg(m_gameProcess->errorString()));
        return false;
    }

    qDebug() << "Java process started, PID:" << m_gameProcess->processId();
    emit processOutput(tr("游戏已启动（进程ID：%1）").arg(m_gameProcess->processId()));
    return true;
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
