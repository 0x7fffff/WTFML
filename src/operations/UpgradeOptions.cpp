/**
 * @file UpgradeOptions.cpp
 * @brief 见头文件：三条合并规矩 + 资源包启用列表重建。
 */
#include "UpgradeOptions.hpp"

#include <QJsonArray>
#include <QJsonDocument>

namespace UpgradeOptions {

Parsed parse(const QString &text) {
    Parsed out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        const QString key = colon < 0 ? line : line.left(colon);
        if (key.isEmpty() || out.values.contains(key)) {
            continue;
        }
        out.order.append(key);
        // 冒号后面那一段**原样**保留（值里可能有冒号，比如语言代码或字符串）。
        out.values.insert(key, colon < 0 ? QString() : line.mid(colon + 1));
    }
    return out;
}

bool isResourcePackKey(const QString &key) {
    return key == QStringLiteral("resourcePacks")
           || key == QStringLiteral("incompatibleResourcePacks");
}

Merge merge(const QString &sourceText, const QString &targetText) {
    Merge out;
    const Parsed source = parse(sourceText);
    const Parsed target = parse(targetText);
    // 以目标那份的顺序当骨架：新版本自己的键顺序保持原样；源里有、目标没有的键**不追加**。
    for (const QString &key : target.order) {
        if (isResourcePackKey(key)) {
            out.skippedKeys.append(key);
            continue;
        }
        if (source.values.contains(key)) {
            out.migratedKeys.append(key);
            out.text += key + QLatin1Char(':') + source.values.value(key) + QLatin1Char('\n');
        } else {
            out.keptTargetKeys.append(key);
            out.text += key + QLatin1Char(':') + target.values.value(key) + QLatin1Char('\n');
        }
    }
    for (const QString &key : source.order) {
        if (isResourcePackKey(key)) {
            if (!out.skippedKeys.contains(key)) {
                out.skippedKeys.append(key);
            }
            continue;
        }
        if (target.values.contains(key)) {
            continue;
        }
        out.unknownKeys.append(key);
    }
    return out;
}

QString resourcePacksLine(const QString &originalLine, const QStringList &migratedPackFiles) {
    // 只保留"真的迁入了的"那些（`file/xxx.zip` 这种相对包名），vanilla 永远在第一个。
    QJsonArray selected;
    selected.append(QStringLiteral("vanilla"));
    const int colon = originalLine.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QJsonDocument document = QJsonDocument::fromJson(
            originalLine.mid(colon + 1).toUtf8());
        if (document.isArray()) {
            for (const QJsonValue &value : document.array()) {
                const QString name = value.toString();
                if (!migratedPackFiles.contains(name)) {
                    continue;
                }
                if (!selected.contains(QJsonValue(name))) {
                    selected.append(name);
                }
            }
        }
    }
    const QByteArray bytes = QJsonDocument(selected).toJson(QJsonDocument::Compact);
    return QStringLiteral("resourcePacks:") + QString::fromUtf8(bytes);
}

QString rewriteKeepingAll(const QString &sourceText, const QStringList &migratedPackFiles) {
    QString text;
    QString packsLine;
    const Parsed parsed = parse(sourceText);
    for (const QString &key : parsed.order) {
        if (isResourcePackKey(key)) {
            if (key == QStringLiteral("resourcePacks")) {
                packsLine = QStringLiteral("resourcePacks:") + parsed.values.value(key);
            }
            continue;
        }
        text += key + QLatin1Char(':') + parsed.values.value(key) + QLatin1Char('\n');
    }
    // 资源包启用列表按"真的迁入了哪些"重建；一个都没迁入时只剩 vanilla。
    text += resourcePacksLine(packsLine, migratedPackFiles) + QLatin1Char('\n');
    return text;
}

}  // namespace UpgradeOptions

