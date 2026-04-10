/**
 * @file IntegrityChecker.cpp
 * @brief 检查安装 Minecraft 的文件夹的完整性。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 提供启动前检查时使用的文件夹有效性验证。
 *
 * 在启动前确保目标版本路径存在。
 */

#include "IntegrityChecker.hpp"

#include <QDir>
#include <QFileInfo>

IntegrityChecker::IntegrityChecker(QObject *parent) : QObject(parent) {}

bool IntegrityChecker::verifyIntegrity(const QString &folderPath, QStringList *errors) const {
    if (errors) {
        errors->clear();
    }

    const QFileInfo pathInfo(folderPath);
    if (!pathInfo.exists() || !pathInfo.isDir()) {
        if (errors) {
            errors->append(tr("指定路径不是有效文件夹：%1").arg(folderPath));
        }
        return false;
    }

    const QDir folder(folderPath);
    if (!folder.exists("versions")) {
        if (errors) {
            errors->append(tr("缺少版本目录：%1").arg(folder.filePath("versions")));
        }
        return false;
    }

    return true;
}
