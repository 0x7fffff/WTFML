#include "IntegrityChecker.h"

#include <QDir>
#include <QFileInfo>

IntegrityChecker::IntegrityChecker(QObject *parent)
    : QObject(parent)
{
}

bool IntegrityChecker::verifyIntegrity(const QString &folderPath, QStringList *errors) const
{
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
