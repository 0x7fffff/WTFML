#pragma once

#include "manage/InstanceFiles.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QLockFile>
#include <memory>

/// 只在产品 main 中启用。持有到进程退出，避免切目录后仍在收尾的任务失去保护。
namespace DirectorySessionLock {
inline bool enabled = false;
inline QHash<QString, std::shared_ptr<QLockFile>> held;

inline QString keyFor(const QString &root) {
    QString path = QDir::fromNativeSeparators(InstanceFiles::canonicalPath(root));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline bool acquire(const QString &root, QString *problem = nullptr) {
    if (!enabled) return true;
    const auto key = keyFor(root);
    if (held.contains(key)) return true;
    auto lock = std::make_shared<QLockFile>(QDir::temp().filePath(
        QStringLiteral("wtfml-directory-%1.lock").arg(key)));
    // 不按运行时长认定锁失效；QLockFile 仍核对 PID/进程是否存在。
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) {
        if (problem) *problem = lock->error() == QLockFile::LockFailedError
            ? QStringLiteral("这个数据目录已被另一份启动器占用，请先关闭那一份窗口。\n%1").arg(root)
            : QStringLiteral("无法取得数据目录的安全锁，未继续操作。\n%1").arg(root);
        return false;
    }
    held.insert(key, std::move(lock));
    return true;
}
} // namespace DirectorySessionLock
