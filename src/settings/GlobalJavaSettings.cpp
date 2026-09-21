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
/// 设置界面「Java 版本」那一项：true = 自动（按版本要求挑），false = 手动钉死。
static constexpr const char kAutoSelect[] = "Java/AutoSelect";
/// 「内存分配」是不是自动档（按系统内存算）。老配置没有这个键，见 load() 的处理。
static constexpr const char kMemoryAuto[] = "Java/MemoryAuto";
/// 实例指定的 Java 与版本要求不符时要不要提醒。老配置没有这个键 = 提醒（默认值）。
static constexpr const char kWarnVersionMismatch[] = "Java/WarnVersionMismatch";

/// 内置默认内存（构造函数里的初值）。自动档算不出结果时的兜底值也是它，
/// load() 判断"用户自己调过没有"时还是拿它当基准 —— 一共就这一处定义。
static constexpr int kBuiltinXmxMb = 4096;
static constexpr int kBuiltinXmsMb = 1024;

/// 自动档的取值范围与步长（和界面那两个框保持一致：MainWindowUI::memoryLimitMaxMb()
/// 与 memoryStepMb()）。数字框里的值必须是 256 的整数倍，否则用户会看到一个 3071。
static constexpr int kAutoXmxMinMb = 1024;
static constexpr int kAutoXmxMaxMb = 8192;
static constexpr int kAutoXmsMinMb = 512;
static constexpr int kAutoXmsMaxMb = 8192;
static constexpr int kAutoStepMb = 256;

/// 向下取整到步长的整数倍。
int roundDownToStep(qint64 valueMb) {
    return static_cast<int>(valueMb / kAutoStepMb * kAutoStepMb);
}

bool runProcess(const QString &program, const QStringList &arguments, QString *stdOut = nullptr,
                QString *stdErr = nullptr, int timeoutMs = 5000) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        return false;
    }

    // NOTE: 这里刻意用 fromLocal8Bit、**不跟着游戏进程一起改成 fromUtf8**。
    // 走这条路的两种调用（java -version、PowerShell）都不受 -Dstdout.encoding 约束
    // —— 那个属性是 JVM 的参数，PowerShell 不是 JVM、不认它。跟着改才是引入乱码。
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

/// 从**裸版本号**（"25.0.1" / "1.8.0_422"）推主版本。
/// "java -version 的输出"和"release 文件里的 JAVA_VERSION"两条路共用这一段，
/// 免得两处各判一次"1.8 该取第二段"、慢慢就判歪一个。
int majorFromVersionString(const QString &version) {
    // NOTE: 这个函数只把**裸版本号**解析成主版本，和"哪个游戏版本要多新的 Java"无关。
    //       那张对照表（1.6-1.16.5 → 8、1.17-1.20.4 → 17、1.20.5-1.21.11 → 21、
    //       26.1+ → 25）不在这里 —— 实际来源是 version.json 的 javaVersion.majorVersion，
    //       由 MainWindow 读取（另有一处兜底值）。
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.isEmpty()) {
        return -1;
    }

    bool ok = false;
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

int parseJavaMajorVersion(const QString &versionOutput) {
    // Java version output examples:
    // java version "1.8.0_301" -> 8
    // java version "11.0.12" -> 11
    // java version "17.0.1" -> 17
    // java version "21.0.1" -> 21

    const QRegularExpression regex(QStringLiteral("version \"([^\"]+)\""));
    const QRegularExpressionMatch match = regex.match(versionOutput);
    if (!match.hasMatch()) {
        return -1;
    }
    return majorFromVersionString(match.captured(1));
}

/**
 * @brief 认出一个 java.exe 的主版本：先读 release 文件，读不到才真的跑一次 java -version。
 * @param javaExe java.exe 的完整路径。
 * @param versionOutput 退回探测时用来接 java -version 的原始输出（可以传 nullptr）。
 * @return 主版本号；exe 不存在、或者两种办法都认不出来时返回 -1。
 *
 * JDK/JRE 安装时就把版本清单写在自己的根目录里（release 文件），读它是一次文件读取
 * （微秒级）；而跑 java -version 要冷启一个 JVM —— Windows 上实测约 80~100 ms，
 * 装了杀软的机器上第一次跑更久。而 scanLocalJavaVersions() 是**同步**跑的（切到设置页
 * 那一下），本机 5 个运行时就是 0.6 s 的界面冻结 —— 这就是"点开设置卡一下"。
 * 能读文件就别开进程。
 *
 * NOTE: 不是每个运行时都有 release。原版启动器那份 jre-legacy（Java 8 的老布局）就只有
 *       bin/ 和 lib/，没有这个文件 —— Java 8 那代还没有这个惯例。所以读不到时**必须**
 *       退回真的跑一次，不能拿目录名（jre-legacy / java-runtime-delta）去猜主版本：
 *       目录名是别人起的，改名或者往里放了别的东西，猜出来的版本就是错的 ——
 *       而"静默选错 Java"正是这一层最怕的错。
 */
int probeJavaMajorVersion(const QString &javaExe, QString *versionOutput = nullptr) {
    if (!QFileInfo::exists(javaExe)) {
        return -1;
    }

    // release 文件在 java.exe 的上一层：<运行时根>/bin/java.exe → <运行时根>/release
    QDir runtimeRoot = QFileInfo(javaExe).absoluteDir();
    runtimeRoot.cdUp();
    QFile releaseFile(runtimeRoot.absoluteFilePath(QStringLiteral("release")));
    if (releaseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const int majorVersion = GlobalJavaSettings::majorVersionFromReleaseFile(
            QString::fromUtf8(releaseFile.readAll()));
        if (majorVersion > 0) {
            return majorVersion;
        }
    }

    // 没有 release 文件（或者里面没有能认的 JAVA_VERSION）：只能真的问它一次。
    QString probed;
    if (!verifyJavaExecutable(javaExe, &probed)) {
        return -1;
    }
    if (versionOutput) {
        *versionOutput = probed;
    }
    return parseJavaMajorVersion(probed);
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
 * NOTE: 为什么必须解析成**绝对路径**（旧实现返回裸命令 "java"，已修 —— 这段留在这里是
 *       免得有人再"简化"回去）：裸命令过不了 verifyJavaExecutable() 第一步的
 *       QFileInfo::exists()；即使侥幸通过，startGame() 也会用 QFileInfo(program).exists()
 *       判定成"Java 可执行文件未找到"。
 */
QString findJavaOnPath(int requiredMajor = -1) {
    const QString resolved = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!resolved.isEmpty() && javaMatchesMajor(resolved, requiredMajor)) {
        return resolved;
    }
    return QString();
}

/// 原版启动器下载好的一个运行时。主版本**在扫描时就认出来带着走** —— 调用方
/// （scanLocalJavaVersions）要用它给 versions 建索引，重跑一遍 java -version
/// 就是让界面白冻一倍时间。
struct VanillaRuntime {
    QString exe;
    int majorVersion = -1;
};

/**
 * @brief 扫描原版启动器已经下载好的运行时目录。
 *
 * 原版启动器会把 Mojang 的运行时解压到
 * %APPDATA%/.minecraft/runtime/<component>/，目录名与 version.json 里的
 * javaVersion.component 一一对应（jre-legacy / java-runtime-alpha / beta /
 * gamma / delta / epsilon）。直接复用这些运行时可以省掉一次完整的下载，
 * 而且它们是 Mojang 针对该游戏版本挑选过的。
 *
 * @return 组件名到该运行时（路径 + 主版本）的映射。认不出主版本的组件不入表 ——
 *         它对调用方没有意义，留着只会让调用方再过滤一遍。
 */
QMap<QString, VanillaRuntime> scanVanillaRuntimeComponents() {
    QMap<QString, VanillaRuntime> components;

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

    // 只收认得出主版本的那些：majorVersion <= 0 的运行时对调用方没有意义，
    // 留在表里还要调用方自己再过滤一遍。
    const QStringList subdirs = runtimeRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subdir : subdirs) {
        const QString exe =
            QDir(runtimeRoot.absoluteFilePath(subdir)).absoluteFilePath("bin/java.exe");
        const int majorVersion = probeJavaMajorVersion(exe);
        if (majorVersion > 0) {
            components[subdir] = VanillaRuntime{exe, majorVersion};
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
 * @return 若下载、解压**并且**在解压结果里找到一个能跑的 java.exe 才返回 true。
 *         解压成功但没找到 java.exe 的会继续试下一个 URL，全都如此就返回 false。
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
      m_xmx(kBuiltinXmxMb)  // 4GB
      ,
      m_xms(kBuiltinXmsMb)  // 1GB
      ,
      m_extraJvmArgs(QStringLiteral("-XX:+UseG1GC")) {
    // load() 里读回的 Xmx/Xms 可能是盘上存的值（手动档），也可能之后被自动档重算掉
    // （第一次读设置页或第一次启动时）。构造函数这里只保证"没读过盘之前也是合法值"。
    load();
}

QString GlobalJavaSettings::javaPath() const { return m_javaPath; }

EffectiveJavaSettings GlobalJavaSettings::effective() const {
    EffectiveJavaSettings settings;
    settings.javaPath = m_javaPath;
    settings.xmx = m_xmx;
    settings.xms = m_xms;
    settings.autoMemory = m_memoryAuto;
    settings.extraJvmArgs = m_extraJvmArgs;
    return settings;
}

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
    // 默认值取成员当前值（构造时是 true）：注册表里没这一项就落到"自动"，
    // 也就是加这个开关之前的行为。
    m_autoSelectJava = settings.value(kAutoSelect, m_autoSelectJava).toBool();

    // 内存自动档对这个键做了一点额外的判断，**必须在读完 Xmx/Xms 之后**：
    //   - 盘上有这个键 → 听用户的。
    //   - 没有这个键（加自动档之前存下来的老配置）→ 值是内置默认值就说明用户没动过它，
    //     按自动处理；不是内置默认值就说明用户自己调过（比如手填了 8192），按手动处理，
    //     免得升级之后静默改掉他已经定好的内存。这条和"识别失败要说出来"是同一个态度：
    //     不确定的时候不改用户的东西。
    m_memoryAuto = settings.contains(kMemoryAuto)
                       ? settings.value(kMemoryAuto).toBool()
                       : (m_xmx == kBuiltinXmxMb && m_xms == kBuiltinXmsMb);

    // 提醒总闸：没有这个键（加它之前存下来的老配置）就落到成员默认值 true，
    // 也就是老行为 —— 那时候这类不匹配是一律报错拦下的，等于"总是提醒"。
    m_warnVersionMismatch = settings.value(kWarnVersionMismatch, m_warnVersionMismatch).toBool();
}

void GlobalJavaSettings::save() const {
    QSettings settings;
    settings.setValue(kJavaPath, m_javaPath);
    settings.setValue(kXmx, m_xmx);
    settings.setValue(kXms, m_xms);
    settings.setValue(kExtraArgs, m_extraJvmArgs);
    settings.setValue(kAutoSelect, m_autoSelectJava);
    settings.setValue(kMemoryAuto, m_memoryAuto);
    settings.setValue(kWarnVersionMismatch, m_warnVersionMismatch);
}

void GlobalJavaSettings::setWarnVersionMismatch(bool warn) {
    m_warnVersionMismatch = warn;
    // 立刻落盘的理由和 setAutoMemory() 一样：界面上就这么一个勾选框，没有「应用」按钮。
    save();
}

void GlobalJavaSettings::setAutoMemory(bool autoMemory) {
    m_memoryAuto = autoMemory;
    // 和 setLauncherBehaviour() 一样：改完立刻落盘。注意**不在这里重算** Xmx/Xms ——
    // 从手动切回自动时由 refreshSuggestedMemory() 算，从自动切到手动时两个框里留着
    // 刚才的建议值当起点（用户是想微调，不是想从默认值重来）。
    save();
}

int GlobalJavaSettings::suggestXmxMb(qint64 totalMb, qint64 availableMb) {
    if (totalMb <= 0) {
        // 读不到系统内存（非 Windows 平台、或者系统调用失败）：退回内置默认值。
        // 这里**不**拿 availableMb 硬凑 —— 连总量都不知道，任何推算都是编的。
        return kBuiltinXmxMb;
    }

    qint64 budget = totalMb / 4;
    if (availableMb > 0) {
        budget = qMin(budget, availableMb / 2);
    }
    // 先在"体面范围"里夹一次。三个参数都写成 qint64：常量是 int，混着传会让 qBound 的
    // 重载解析变成二义的。
    budget = qBound(qint64(kAutoXmxMinMb), budget, qint64(kAutoXmxMaxMb));
    // 再套物理内存一半的硬顶。这一条**排在下限之后**是有意的：小机器上它会压过下限
    // （2 GB 的机器给 1024 已经是一半了，1 GB 的机器只该给 512）—— 在一条本来就紧张的
    // 机器上按"最少 1 GB"硬凑，只会让 JVM 连堆都开不起来，比给个小值更糟。
    budget = qMin(budget, totalMb / 2);
    // 但绝不小于 JVM 侧的下限（setXmx 会钳到 256）：返回一个会被 setter 悄悄改掉的值，
    // 就是"界面显示的和实际用的不一致"。
    return qMax(roundDownToStep(budget), 256);
}

int GlobalJavaSettings::suggestXmsMb(int xmxMb) {
    // Xms 给 Xmx 的四分之一：太小会一直扩容（老版本的卡顿来源），太大等于一启动就占满。
    //
    // Xms **必须** <= Xmx，否则 JVM 直接起不来：
    //   Error occurred during initialization of VM
    //   Initial heap size set to a larger value than the maximum heap size
    // 所以下限和上限要一起排好序再夹：Xmx 比 512 还小的时候（setXmx 的下限是 256），
    // 下限得跟着退到 Xmx 上，直接写 qBound(512, x, xmx) 会变成 min > max，取到的值没有意义。
    const int xmx = qMax(xmxMb, 256);
    const int upper = qMin(xmx, kAutoXmsMaxMb);
    const int quarter = roundDownToStep(xmx / 4);
    return qBound(qMin(kAutoXmsMinMb, upper), quarter, upper);
}

SystemMemory::Usage GlobalJavaSettings::refreshSuggestedMemory() {
    const SystemMemory::Usage usage = SystemMemory::read();
    if (!m_memoryAuto) {
        // 手动档：读还是要读（界面拿它写说明），但一个数字都不许动。
        return usage;
    }
    setXmx(suggestXmxMb(usage.totalMb, usage.availableMb));
    setXms(suggestXmsMb(m_xmx));
    return usage;
}

int GlobalJavaSettings::majorVersionFromReleaseFile(const QString &releaseText) {
    // release 文件是 KEY="value" 一行一条，版本那条叫 JAVA_VERSION：
    //   Java 9+ : JAVA_VERSION="21.0.7"
    //   Java 8  : JAVA_VERSION="1.8.0_422"
    // 刻意**只认带引号的 JAVA_VERSION=** 这一种写法：同一个文件里还有
    // JAVA_VERSION_DATE、MODULES 之类的行，宽泛地找 "version" 会撞上它们。
    // 找不到就返回 -1，调用方退回真的跑一次 java -version，不猜。
    static const QRegularExpression regex(QStringLiteral("JAVA_VERSION=\"([^\"]+)\""));
    const QRegularExpressionMatch match = regex.match(releaseText);
    if (!match.hasMatch()) {
        return -1;
    }
    return majorFromVersionString(match.captured(1));
}

QMap<int, QString> GlobalJavaSettings::scanLocalJavaVersions() {
    QMap<int, QString> versions;
    const QString javaDir = AppPaths::javaDir();

    QDir dir(javaDir);
    if (dir.exists()) {
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &subdir : subdirs) {
            const QString javaExe = dir.absoluteFilePath(subdir + "/bin/java.exe");
            const int majorVersion = probeJavaMajorVersion(javaExe);
            if (majorVersion > 0) {
                versions[majorVersion] = javaExe;
            }
        }
    }

    // 原版启动器已经下载好的运行时（jre-legacy / java-runtime-*）同样可以复用，
    // 省掉一次完整下载。这里用 first-wins，优先保留启动器自己下载的那一份。
    //
    // NOTE: 主版本在 scanVanillaRuntimeComponents() 里就已经认出来了，这里**不重跑**。
    //       以前这一层对每个组件又跑了一遍 java -version，同一件事做两遍 ——
    //       本机 4 个组件就是多冻 0.3 s，而且这 0.3 s 一个字的信息都没换来。
    const QMap<QString, VanillaRuntime> vanillaComponents = scanVanillaRuntimeComponents();
    for (auto it = vanillaComponents.cbegin(); it != vanillaComponents.cend(); ++it) {
        const VanillaRuntime &runtime = it.value();
        if (!versions.contains(runtime.majorVersion)) {
            qDebug() << "Reusing vanilla launcher runtime" << it.key() << "for Java"
                     << runtime.majorVersion;
            versions[runtime.majorVersion] = runtime.exe;
        }
    }

    return versions;
}

bool GlobalJavaSettings::downloadJavaFromMojang(int majorVersion, QString *javaPathOut) {
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

    // 在 Windows x64 的包里挑一个：优先 JRE，没有 JRE 才退到 JDK（见下面的 downloadUrl 选取）
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
        // 只回填路径，不碰 m_javaPath、不落盘 —— 见 .hpp 里的 NOTE：存到哪个作用域
        // 是调用方的事。
        if (javaPathOut != nullptr) {
            *javaPathOut = javaPath;
        }
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
    // 老签名 = **全局档**：挑中的路径写回本对象并落盘。除了"值从成员换成一份副本"，
    // 这一步的行为和加实例覆盖之前逐字节一致（E 段那几条断言钉的就是它）。
    EffectiveJavaSettings io = effective();
    QString error;
    if (!ensureJavaForVersion(io, majorVersion, component, m_autoSelectJava, &error)) {
        return false;
    }
    adoptJavaPath(io.javaPath);
    return true;
}

void GlobalJavaSettings::adoptJavaPath(const QString &path) {
    if (path == m_javaPath) {
        return;
    }
    m_javaPath = path;
    save();
}

bool GlobalJavaSettings::ensureJavaForVersion(EffectiveJavaSettings &io, int majorVersion,
                                              const QString &component, bool allowAutoSelect,
                                              QString *errorOut, bool allowMismatchedPath) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    qDebug() << "Ensuring Java for version:" << majorVersion << "component:" << component;

    // First, check if we already have the correct version configured
    if (!io.javaPath.isEmpty()) {
        // 用户在「Java 版本不符」那个框里点了「仍然用这个 Java 启动」：他要的就是这条路径。
        // 判在 javaMatchesMajor() **之前**是有意的 —— 那个函数总是冷启一个 JVM，而这里
        // 已经拿到过用户的明确授权，没必要再花那几十毫秒去确认一件他已经决定的事。
        if (allowMismatchedPath) {
            qDebug() << "Using the mismatched Java the user explicitly chose:" << io.javaPath;
            return true;
        }
        if (javaMatchesMajor(io.javaPath, majorVersion)) {
            qDebug() << "Using configured Java version" << majorVersion << "at:" << io.javaPath;
            return true;
        }
        qDebug() << "Configured Java does not match required version" << majorVersion << ":"
                 << io.javaPath;
    }

    // 手动档到此为止：配置的路径不匹配（或者压根没配）时**不替用户换一个**。
    // 自动挑在手动档下是帮倒忙 —— 用户钉 Java 8 就是为了跑 1.16.5，静默换成 Java 21
    // 只会得到 UnsupportedClassVersionError，而且看不出是启动器换的。报错好过猜。
    if (!allowAutoSelect) {
        const QString reason =
            QStringLiteral("Java 选的是「手动指定」，而")
            + (io.javaPath.isEmpty() ? QStringLiteral("还没指定路径")
                                     : io.javaPath + QStringLiteral(" 的主版本不是 ")
                                           + QString::number(majorVersion))
            + QStringLiteral(" —— 不自动更换，直接失败。");
        qWarning() << reason;
        return fail(reason);
    }

    // Scan local Java versions
    QMap<int, QString> localVersions = scanLocalJavaVersions();
    if (localVersions.contains(majorVersion)) {
        const QString path = localVersions[majorVersion];
        qDebug() << "Found matching local Java version" << majorVersion << "at:" << path;
        io.javaPath = path;
        return true;
    }

    // Try to find in environment
    QString candidate = findJavaInEnvironment(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java in environment:" << candidate;
        io.javaPath = candidate;
        return true;
    }

    candidate = findJavaOnPath(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java on PATH:" << candidate;
        io.javaPath = candidate;
        return true;
    }

    candidate = findJavaInCommonLocations(majorVersion);
    if (!candidate.isEmpty()) {
        qDebug() << "Found matching Java in common locations:" << candidate;
        io.javaPath = candidate;
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
            io.javaPath = downloaded;
            return true;
        }
        qDebug() << "Component" << component << "did not provide a matching Java, falling back";
    }

    // Download from Adoptium as a last resort
    qDebug() << "No matching Java found, downloading version" << majorVersion << "from Adoptium...";
    QString downloaded;
    // NOTE: 这一条**不**再认一次主版本 —— 老代码也没有（downloadJavaFromMojang 内部
    //       已经确认解压出来的 java.exe 能跑，而 Adoptium 是照 majorVersion 要的包）。
    //       这里多判一次会让"本来能启动"变成"报错"，那不是这次改动该带来的差别。
    if (downloadJavaFromMojang(majorVersion, &downloaded)) {
        qDebug() << "Downloaded Java version" << majorVersion;
        io.javaPath = downloaded;
        return true;
    }

    qDebug() << "Failed to obtain Java version" << majorVersion;
    return fail(QStringLiteral("找不到也下载不到 Java %1。").arg(majorVersion));
}
