#include "manage/InstanceMetaFile.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

namespace InstanceMetaFile {
namespace {
QString message(const char *text) {
    return QCoreApplication::translate("InstanceMetaFile", text);
}

/// 版本号在报错里怎么给人看：数字照原样（2、1.5、0），字符串带引号，别的说"不是数字"。
QString describeVersion(const QJsonValue &value) {
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 17);
    if (value.isString()) return QStringLiteral("「%1」").arg(value.toString());
    return message("不是数字");
}
} // namespace

QJsonObject read(const QString &path, Status *statusOut) {
    if (statusOut != nullptr) *statusOut = Status::Missing;

    QFile file(path);
    if (!file.exists()) {
        // 绝大多数版本没有这个文件，不是异常，也别打日志 —— refreshInstalledVersions()
        // 会对着每个已安装版本调一次，打日志就是刷屏。
        return QJsonObject();
    }
    const auto unreadable = [statusOut] {
        if (statusOut != nullptr) *statusOut = Status::Unreadable;
        return QJsonObject();
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > kSizeLimit) {
        qWarning() << "Version metadata exists but cannot be read:" << path;
        return unreadable();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 手写的文件写错格式很常见。整个文件当没有，但不静默 —— 这是用户手写的，
        // 不告诉他写错了他会一直以为生效了。
        qWarning() << "Failed to parse version metadata" << path << ":" << parseError.errorString();
        return unreadable();
    }

    const QJsonObject object = document.object();
    const QJsonValue version = object.value(QString::fromLatin1(kSchemaKey));
    if (!version.isUndefined() && !(version.isDouble() && version.toDouble() == double(kSchemaVersion))) {
        // 内容照常给出去（读侧不该因为一个版本号就看不见自己的显示名），但要吭声：
        // 这多半是更新的启动器写的，用户该知道老版本不会再改它。
        qWarning() << "Version metadata has an unknown schema version" << path << version;
        if (statusOut != nullptr) *statusOut = Status::BadVersion;
        return object;
    }

    if (statusOut != nullptr) *statusOut = Status::Ok;
    return object;
}

bool mergeWrite(const QString &path, const QJsonObject &fields, QString *errorOut) {
    const auto refuse = [errorOut](const QString &why) {
        if (errorOut != nullptr) *errorOut = why;
        return false;
    };

    Status status = Status::Missing;
    QJsonObject merged = read(path, &status);
    if (status == Status::Unreadable) {
        // 往一份读不懂的内容上合并 = 把用户手写的部分丢掉。宁可这次不写。
        return refuse(message("%1 读不出来（打不开、内容损坏或过大），"
                              "为避免覆盖掉你写的东西，这次没有改动它。")
                          .arg(path));
    }
    if (status == Status::BadVersion) {
        return refuse(message("%1 的格式版本是 %2，这个启动器只认 %3；"
                              "为免写坏它，这次没有改动。")
                          .arg(path, describeVersion(merged.value(QString::fromLatin1(kSchemaKey))),
                               QString::number(kSchemaVersion)));
    }

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (it.value().isNull() || it.value().isUndefined()) {
            merged.remove(it.key());
        } else {
            merged.insert(it.key(), it.value());
        }
    }
    // 版本号最后盖：调用方就算在 fields 里写了它，也写不出一个我们不认识的版本。
    merged.insert(QString::fromLatin1(kSchemaKey), kSchemaVersion);

    // 目录理论上一定在（扫描是先认 jar 的），但重命名 / 复制 / 升级那几条路会在目录
    // 刚建好时调过来，建不出来就没什么好写的了。
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        return refuse(message("无法创建目录 %1。").arg(QDir::toNativeSeparators(directory)));
    }

    // QSaveFile：先写同目录的临时文件、commit() 时才改名覆盖。断电或崩溃只会留下
    // "旧内容完好"，绝不会留下半截 JSON —— 这个文件坏掉的后果是那个实例的显示名和
    // 参数整体失效，比"这次没写成"糟得多。
    //
    // 缩进输出：这是用户会拿记事本打开看的文件，紧排一行没法读。代价是重写会把
    // 用户原来的排版统一掉，可以接受。
    const QByteArray bytes = QJsonDocument(merged).toJson(QJsonDocument::Indented);
    if (bytes.size() > kSizeLimit) {
        return refuse(message("%1 写出来太大了，这次没有改动。").arg(path));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return refuse(message("无法写入 %1。").arg(path));
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return refuse(message("写入 %1 失败。").arg(path));
    }
    return true;
}
} // namespace InstanceMetaFile
