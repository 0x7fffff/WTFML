#include "InstalledModInventory.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>

namespace Presets {
InstalledModInventory scanInstalledMods(const QString &root, const std::atomic_bool *cancel) {
    InstalledModInventory result;
    const auto fail = [&result](const QString &text) { result.problem = text; return result; };
    const QString modsPath = QDir(root).filePath(QStringLiteral("mods"));
    if (!QDir::isAbsolutePath(root) || !QFileInfo(root).isDir() || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()
        || QFileInfo(modsPath).isSymLink() || QFileInfo(modsPath).isJunction())
        return fail(QCoreApplication::translate("InstalledModInventory", "实例或模组目录不安全，未读取已安装模组。"));
    if (QFileInfo(modsPath).exists() && !QFileInfo(modsPath).isDir())
        return fail(QCoreApplication::translate("InstalledModInventory", "模组路径不是目录，未允许安装。"));
    QJsonArray records;
    const QString auditPath = QDir(root).filePath(QStringLiteral("wtfml.preset-mod-install.json"));
    QFile audit(auditPath);
    if (!QFileInfo(auditPath).isSymLink() && audit.open(QIODevice::ReadOnly) && audit.size() <= 8 * 1024 * 1024) {
        const auto document = QJsonDocument::fromJson(audit.readAll()).object();
        if (document.value(QStringLiteral("format")).toString() == QStringLiteral("wtfml-preset-mod-result/1"))
            records = document.value(QStringLiteral("plannedFiles")).toArray();
    }
    QElapsedTimer timer;
    timer.start();
    QHash<QString, QList<QJsonObject>> byName;
    for (const auto &record : records) {
        const auto object = record.toObject();
        byName[object.value(QStringLiteral("filename")).toString()].append(object);
    }
    const auto entries = QDir(modsPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &entry : entries) {
        if (entry.suffix().compare(QStringLiteral("jar"), Qt::CaseInsensitive) != 0) continue;
        if (result.files.size() >= 10000) return fail(QCoreApplication::translate("InstalledModInventory", "已安装模组超过安全扫描上限（10000 项）。"));
        if (entry.isSymLink() || entry.isJunction()) return fail(QCoreApplication::translate("InstalledModInventory", "模组文件是目录链接，未允许安装：%1").arg(entry.fileName()));
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) return fail(QCoreApplication::translate("InstalledModInventory", "无法读取已安装模组：%1").arg(entry.fileName()));
        QCryptographicHash hash(QCryptographicHash::Sha1);
        while (!file.atEnd()) {
            if ((cancel && cancel->load(std::memory_order_relaxed)) || timer.elapsed() > 600000)
                return fail(QCoreApplication::translate("InstalledModInventory", "已安装模组扫描已取消或超时。"));
            const auto bytes = file.read(1024 * 1024);
            if (bytes.isEmpty()) return fail(QCoreApplication::translate("InstalledModInventory", "读取已安装模组失败：%1").arg(entry.fileName()));
            hash.addData(bytes);
        }
        InstalledModFile found;
        found.path = entry.absoluteFilePath();
        found.sha1 = QString::fromLatin1(hash.result().toHex());
        for (const auto &object : byName.value(entry.fileName())) {
            if (object.value(QStringLiteral("sha1")).toString().compare(found.sha1, Qt::CaseInsensitive) != 0) continue;
            const QString key = object.value(QStringLiteral("project")).toString();
            const QString version = object.value(QStringLiteral("versionId")).toString();
            if (key.startsWith(QStringLiteral("modrinth:")) || key.startsWith(QStringLiteral("curseforge:"))) {
                if (!found.projectKey.isEmpty() && (found.projectKey != key
                    || (!found.versionId.isEmpty() && !version.isEmpty() && found.versionId != version)))
                    return fail(QCoreApplication::translate("InstalledModInventory", "模组安装记录的身份相互冲突：%1").arg(entry.fileName()));
                found.projectKey = key;
                if (!version.isEmpty()) found.versionId = version;
            }
        }
        result.files.append(found);
    }
    if (cancel && cancel->load(std::memory_order_relaxed)) return fail(QCoreApplication::translate("InstalledModInventory", "已安装模组扫描已取消。"));
    return result;
}

bool sameInstalledMods(const InstalledModInventory &left, const InstalledModInventory &right) {
    if (!left.problem.isEmpty() || !right.problem.isEmpty() || left.files.size() != right.files.size()) return false;
    for (qsizetype i = 0; i < left.files.size(); ++i)
        if (left.files[i].path != right.files[i].path || left.files[i].sha1 != right.files[i].sha1) return false;
    return true;
}
}
